#include "button.h"
#include "cJSON.h"
#include "ieee802154.h"
#include "keypress.pb.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"
#include "wifi.h"
#include <errno.h>
#include <pb_encode.h>
#include <string.h>

// uncomment desired encoding
// #define ENCODING_JSON
// #define ENCODING_TLV
#define ENCODING_PROTOBUF

// ---- Communication modes -------------------------------------------
// Uncomment ONE of the following to choose the transport mode:
// #define ENABLE_WIFI_UDP      // WiFi + UDP broadcast
#define ENABLE_IEEE802154 // IEEE 802.15.4 radio (no WiFi needed)

#define BUTTON_TASK_STACKSIZE 2048
#define HANDLER_TASK_STACKSIZE 4096
#define MAX_RETRIES 3
#define RETRY_TIMEOUT_MS 200

// 802.15.4 network config — PAN_ID must match on all devices, MY_ADDR must be unique per device
#define PGP_PAN_ID  0xABCD
#define MY_ADDR     0x0001 // change to 0x0002 on the second device

// ---- Protocol types ------------------------------------------------

typedef enum : uint8_t {
    MSG_KEY_PRESS = 0x01,
    MSG_LIGHT_STATE = 0x02,
    MSG_ACK = 0x03,
    MSG_HEARTBEAT = 0x04,
} msg_type_t;

typedef enum : uint8_t {
    KEY_RELEASED = 0,
    KEY_PRESSED = 1,
} key_state_t;

// Packed so no paddings
typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t msg_type;
    uint16_t seq;
    uint8_t dev_id;
    uint16_t payload_len;
} protocol_header_t;

typedef struct {
    bool pressed;
    uint32_t timestamp;
} button_event_t;

// ---- Globals -------------------------------------------------------

static const char* TAG = "Main";
QueueHandle_t button_event_queue;
static uint16_t g_seq = 0; // global sequence counter, increments per send

// IEEE 802.15.4 RX frame queue
QueueHandle_t ieee802154_rx_queue = NULL;

typedef struct {
    uint8_t data[256];
    uint16_t len;
} ieee802154_rx_frame_t;

// ---- Encoding: TLV -------------------------------------------------

#define TLV_KEY_ID 0x01 // defined by protocol
#define TLV_KEY_STATE 0x02
#define TLV_TIMESTAMP 0x03

static int encode_tlv_keypress(uint8_t* out, size_t out_len, uint8_t key_id, key_state_t state,
                               uint32_t timestamp_ms) {
    if (out_len < 12)
        return -1;
    uint8_t* p = out;

    // *p++ = assign type, len or value to current pointer address and then advance
    // since p is a uint8_t-pointer, it advances by exactly one byte
    // TLV stands for Type, Length and Value

    *p++ = TLV_KEY_ID; // type
    *p++ = 1;          // length
    *p++ = key_id;     // value

    *p++ = TLV_KEY_STATE;
    *p++ = 1;
    // cast enum to uint8_t, enums are int-sized by default, we only want 1 byte
    *p++ = (uint8_t)state;

    // 4-byte timestamp, big-endian
    *p++ = TLV_TIMESTAMP;
    *p++ = 4;
    *p++ = (timestamp_ms >> 24) & 0xFF;
    *p++ = (timestamp_ms >> 16) & 0xFF;
    *p++ = (timestamp_ms >> 8) & 0xFF;
    *p++ = (timestamp_ms) & 0xFF;

    // pointer subtraction yields ptrdiff_t, cast to match return type int
    return (int)(p - out); // 12 bytes
}

// ---- Encoding: JSON ------------------------------------------------

static int encode_json_keypress(uint8_t* out, size_t out_len, uint8_t key_id, key_state_t state,
                                uint32_t timestamp_ms) {
    // build json object {} on heap
    cJSON* root = cJSON_CreateObject();
    if (!root)
        return -1;

    // build object like {"key_id":0,"state":1,"ts":12345}
    cJSON_AddNumberToObject(root, "key_id", key_id);
    cJSON_AddNumberToObject(root, "state", (int)state);
    cJSON_AddNumberToObject(root, "ts", timestamp_ms);

    // renders json object into heap allocated string
    // and deletes object, since we don't need it anymore
    char* rendered = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!rendered)
        return -1;

    // guards against fails on serialization
    // checks if the rendered string actually fits into output buffer
    int len = (int)strlen(rendered);
    if (len >= (int)out_len) {
        free(rendered);
        return -1;
    }

    // copies string into out and frees heap
    memcpy(out, rendered, len);
    free(rendered);
    return len;
}

// ---- Encoding: ProtoBuf --------------------------------------------

static int encode_protobuf_keypress(uint8_t* out, size_t out_len, uint8_t key_id, key_state_t state,
                                    uint32_t timestamp_ms) {
    KeyPress msg = KeyPress_init_zero; // generated struct, zero all fields
    msg.key_id = key_id;
    msg.state = (uint32_t)state;
    msg.ts = timestamp_ms;

    pb_ostream_t stream = pb_ostream_from_buffer(out, out_len);
    if (!pb_encode(&stream, KeyPress_fields, &msg))
        return -1;

    return (int)stream.bytes_written; // typically ~6 bytes
}

// ---- Send helper ---------------------------------------------------

static void protocol_send(int sock, struct sockaddr_in* dest, msg_type_t type, uint8_t* payload,
                          uint16_t plen, uint16_t seq) {
    uint8_t buf[256];
    // return if header + payload larger than buf
    if (sizeof(protocol_header_t) + plen > sizeof(buf))
        return;

    // cast buffer as protocol header and assigns values
    protocol_header_t* hdr = (protocol_header_t*)buf;
    hdr->version = 0x01;
    hdr->msg_type = (uint8_t)type;
    // htons converts integers from host byte-order (little endian) to network byte-order (big
    // endiand)
    hdr->seq = htons(seq);
    hdr->dev_id = 0x01;
    hdr->payload_len = htons(plen);

    // appends payload into buffer, right after header
    memcpy(buf + sizeof(protocol_header_t), payload, plen);

    int total = sizeof(protocol_header_t) + plen;
    int err = sendto(sock, buf, total, 0, (struct sockaddr*)dest, sizeof(*dest));

    if (err < 0) {
        ESP_LOGE(TAG, "sendto failed: errno %d", errno);
    } else {
        ESP_LOGI(TAG, "Sent %d bytes (seq=%u, type=0x%02X)", total, seq, type);
    }
}

// ---- Tasks ---------------------------------------------------------

void button_task(void* arg) {
    static bool last_state = false;
    while (1) {
        refresh_button_value();
        if (btn_pressed != last_state) {
            button_event_t ev = {
                .pressed = btn_pressed,
                .timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS,
            };
            xQueueSend(button_event_queue, &ev, portMAX_DELAY);
        }
        last_state = btn_pressed;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void button_handler_802154_task(void* arg) {
    button_event_t event;
    uint8_t payload[128];
    uint8_t frame_buf[256];

    ESP_LOGI(TAG, "IEEE 802.15.4 Handler Task started");

    // Frame parameters — broadcast so every device in the PAN receives the frame
    uint16_t dest_pan_id = PGP_PAN_ID;
    uint16_t dest_addr   = 0xFFFF; // 802.15.4 broadcast address
    uint16_t src_pan_id  = PGP_PAN_ID;
    uint16_t src_addr    = MY_ADDR;

    while (1) {
        if (xQueueReceive(button_event_queue, &event, portMAX_DELAY) != pdTRUE)
            continue;

        key_state_t state = event.pressed ? KEY_PRESSED : KEY_RELEASED;
        ESP_LOGI(TAG, "Button %s at %lums (802.15.4)", event.pressed ? "PRESSED" : "RELEASED",
                 event.timestamp);

        // --- Encode payload
        int plen = -1;

#if defined(ENCODING_TLV)
        plen = encode_tlv_keypress(payload, sizeof(payload), /*key_id=*/0, state, event.timestamp);

#elif defined(ENCODING_JSON)
        plen = encode_json_keypress(payload, sizeof(payload), /*key_id=*/0, state, event.timestamp);

#elif defined(ENCODING_PROTOBUF)
        plen = encode_protobuf_keypress(payload, sizeof(payload), /*key_id=*/0, state,
                                        event.timestamp);
#endif

        if (plen < 0) {
            ESP_LOGE(TAG, "Encoding failed");
            continue;
        }

        uint8_t seq = g_seq++; // Increment global sequence counter

        // --- Build IEEE 802.15.4 frame
        int frame_len = ieee802154_build_frame(seq,              // Sequence number
                                               dest_pan_id,      // Destination PAN ID
                                               dest_addr,        // Destination address (2 bytes)
                                               src_pan_id,       // Source PAN ID
                                               src_addr,         // Source address (2 bytes)
                                               payload,          // Payload
                                               (uint16_t)plen,   // Payload length
                                               frame_buf,        // Output buffer
                                               sizeof(frame_buf) // Buffer size
        );

        if (frame_len < 0) {
            ESP_LOGE(TAG, "Failed to build frame");
            continue;
        }

        // --- Send IEEE 802.15.4 frame
        esp_err_t ret = ieee802154_send(frame_buf, (uint16_t)frame_len);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "802.15.4 frame sent (seq=%u, len=%d)", seq, frame_len);
        } else {
            ESP_LOGE(TAG, "Failed to send 802.15.4 frame");
        }
    }
}

void button_receiver_802154_task(void* arg) {
    uint8_t rx_buf[256];
    ieee802154_frame_t frame;
    ieee802154_rx_frame_t rx_frame;

    ESP_LOGI(TAG, "IEEE 802.15.4 Receiver Task started");

    while (1) {
        // Wait for frame from RX queue (populated by ISR callback)
        if (xQueueReceive(ieee802154_rx_queue, &rx_frame, portMAX_DELAY) == pdTRUE) {
            // Parse the received frame
            if (ieee802154_parse_frame(rx_frame.data, rx_frame.len, &frame) == 0) {
                ESP_LOGI(TAG,
                         "RX: SEQ=%u | DEST: PAN=0x%04X ADDR=0x%04X | SRC: ADDR=0x%04X | "
                         "Payload=%d bytes",
                         frame.sequence, frame.dest_pan_id, frame.dest_addr, frame.src_addr,
                         frame.payload_len);

                // Log payload in hex if present
                if (frame.payload_len > 0 && frame.payload_len <= 64) {
                    ESP_LOGI(TAG, "RX Payload (hex): ");
                    for (int i = 0; i < frame.payload_len; i++) {
                        printf("%02X ", frame.payload[i]);
                    }
                    printf("\n");
                }
            } else {
                ESP_LOGW(TAG, "Failed to parse received frame");
            }
        }
    }
}

void button_handler_task(void* arg) {
    button_event_t event;
    uint8_t payload[128];

    // --- Socket setup
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket");
        vTaskDelete(NULL);
        return;
    }

    int enable = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable));

    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(netif, &ip_info);
    uint32_t broadcast = ip_info.ip.addr | ~ip_info.netmask.addr;

    struct sockaddr_in dest = {
        .sin_family = AF_INET,
        .sin_port = htons(CONFIG_BROADCAST_PORT),
        .sin_addr.s_addr = broadcast,
    };

    ESP_LOGI(TAG, "UDP ready → %s:%d", inet_ntoa(dest.sin_addr), ntohs(dest.sin_port));

    // bind so the socket can receive incoming packets (e.g. ACKs)
    struct sockaddr_in local = {
        .sin_family = AF_INET,
        .sin_port = htons(CONFIG_BROADCAST_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr*)&local, sizeof(local)) < 0) {
        ESP_LOGE(TAG, "bind failed: errno %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    // set 50ms receive timeout instead of blocking forever
    struct timeval timeout = {
        .tv_sec = 0,
        .tv_usec = RETRY_TIMEOUT_MS * 10000,
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    while (1) {
        if (xQueueReceive(button_event_queue, &event, portMAX_DELAY) != pdTRUE)
            continue;

        key_state_t state = event.pressed ? KEY_PRESSED : KEY_RELEASED;
        ESP_LOGI(TAG, "Button %s at %lums", event.pressed ? "PRESSED" : "RELEASED",
                 event.timestamp);

        // --- Encode
        int plen = -1;

#if defined(ENCODING_TLV)
        plen = encode_tlv_keypress(payload, sizeof(payload), /*key_id=*/0, state, event.timestamp);

#elif defined(ENCODING_JSON)
        plen = encode_json_keypress(payload, sizeof(payload), /*key_id=*/0, state, event.timestamp);

#elif defined(ENCODING_PROTOBUF)
        plen = encode_protobuf_keypress(payload, sizeof(payload), /*key_id=*/0, state,
                                        event.timestamp);
#endif

        if (plen < 0) {
            ESP_LOGE(TAG, "Encoding failed");
            continue;
        }

        uint16_t sent_seq = g_seq++; // increment once here, all retries reuse same seq
        int attempts = event.pressed ? MAX_RETRIES : 1;

        for (int i = 0; i < attempts; i++) {
            protocol_send(sock, &dest, MSG_KEY_PRESS, payload, (uint16_t)plen, sent_seq);

            if (!event.pressed)
                break;

            uint8_t ack_buf[sizeof(protocol_header_t)];
            int received = recvfrom(sock, ack_buf, sizeof(ack_buf), 0, NULL, NULL);

            if (received >= (int)sizeof(protocol_header_t)) {
                protocol_header_t* ack_hdr = (protocol_header_t*)ack_buf;
                if (ack_hdr->msg_type == MSG_ACK && ntohs(ack_hdr->seq) == sent_seq) {
                    ESP_LOGI(TAG, "ACK received for seq=%u", sent_seq);
                    break;
                }
            }

            ESP_LOGW(TAG, "No ACK for seq=%u, retry %d/%d", sent_seq, i + 1, MAX_RETRIES);
        }
    }

    close(sock);
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());

    button_event_queue = xQueueCreate(10, sizeof(button_event_t));

    // Start button polling task (works for both WiFi and IEEE 802.15.4)
    xTaskCreate(button_task, "BTN_POLL", BUTTON_TASK_STACKSIZE, NULL, 3, NULL);

#if defined(ENABLE_WIFI_UDP)
    // WiFi + UDP broadcast mode
    ESP_LOGI(TAG, "=== Starting WiFi + UDP mode ===");
    EventGroupHandle_t wifi_event_group = wifi_init();
    ESP_LOGI(TAG, "Waiting for Wi-Fi...");
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "Wi-Fi connected.");

    // Start WiFi handler task
    xTaskCreate(button_handler_task, "BTN_HANDLER", HANDLER_TASK_STACKSIZE, NULL, 3, NULL);

#elif defined(ENABLE_IEEE802154)
    // IEEE 802.15.4 mode (no WiFi needed)
    ESP_LOGI(TAG, "=== Starting IEEE 802.15.4 mode ===");

    // Initialize IEEE 802.15.4 on channel 15
    ESP_ERROR_CHECK(ieee802154_init(15, PGP_PAN_ID, MY_ADDR));
    ESP_LOGI(TAG, "IEEE 802.15.4 initialized, ready to send");

    // Create RX queue for received frames
    ieee802154_rx_queue = xQueueCreate(10, sizeof(ieee802154_rx_frame_t));

    // Start IEEE 802.15.4 transmitter task
    xTaskCreate(button_handler_802154_task, "BTN_802154_TX", HANDLER_TASK_STACKSIZE, NULL, 3, NULL);

    // Start IEEE 802.15.4 receiver task
    xTaskCreate(button_receiver_802154_task, "BTN_802154_RX", HANDLER_TASK_STACKSIZE, NULL, 3,
                NULL);

    // Enable receiver to start listening
    ESP_ERROR_CHECK(ieee802154_rx_enable());

#else
#error "Please define either ENABLE_WIFI_UDP or ENABLE_IEEE802154 in main.c"
#endif
}