#include "esp_log.h"
#include "ieee802154.h"
#include "ieee802154_receiver.h"
#include "keypress.pb.h"
#include "pb_decode.h"
#include <string.h>

static const char* TAG = "IEEE802154_RX";

// Protocol message types (must match sender)
typedef enum {
    MSG_KEY_PRESS = 0x01,
    MSG_LIGHT_STATE = 0x02,
    MSG_ACK = 0x03,
    MSG_HEARTBEAT = 0x04,
} msg_type_t;

// Protocol header structure (packed, no padding)
typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t msg_type;
    uint16_t seq;
    uint8_t dev_id;
    uint16_t payload_len;
} protocol_header_t;

static void ieee802154_rx_callback(const ieee802154_frame_t* frame, int8_t rssi) {
    if (!frame || !frame->payload || frame->payload_len < sizeof(protocol_header_t)) {
        ESP_LOGW(TAG, "Invalid frame received");
        return;
    }

    // Parse protocol header from payload
    protocol_header_t* hdr = (protocol_header_t*)frame->payload;
    uint8_t* msg_payload = frame->payload + sizeof(protocol_header_t);
    uint16_t msg_payload_len = frame->payload_len - sizeof(protocol_header_t);

    // Byte-swap seq from network byte order if needed
    uint16_t seq = (hdr->seq >> 8) | ((hdr->seq & 0xFF) << 8); // simple byte swap

    ESP_LOGI(TAG, "Frame RX: ver=%u type=0x%02X seq=%u dev=%u payload_len=%u rssi=%d", hdr->version,
             hdr->msg_type, seq, hdr->dev_id, msg_payload_len, rssi);

    switch (hdr->msg_type) {
    case MSG_KEY_PRESS: {
        // Decode protobuf KeyPress message
        KeyPress keypress = KeyPress_init_zero;
        pb_istream_t stream = pb_istream_from_buffer(msg_payload, msg_payload_len);

        if (!pb_decode(&stream, KeyPress_fields, &keypress)) {
            ESP_LOGE(TAG, "Failed to decode KeyPress: %s", PB_GET_ERROR(&stream));
            break;
        }

        const char* state_str = (keypress.state == 1) ? "PRESSED" : "RELEASED";
        ESP_LOGI(TAG, "KEY_PRESS: key_id=%u state=%s ts=%u ms", keypress.key_id, state_str,
                 keypress.ts);
        break;
    }

    case MSG_LIGHT_STATE:
        ESP_LOGI(TAG, "LIGHT_STATE: payload_len=%u", msg_payload_len);
        break;

    case MSG_ACK:
        ESP_LOGI(TAG, "ACK received for seq=%u", seq);
        break;

    case MSG_HEARTBEAT:
        ESP_LOGI(TAG, "HEARTBEAT from device %u", hdr->dev_id);
        break;

    default:
        ESP_LOGW(TAG, "Unknown message type 0x%02X", hdr->msg_type);
        break;
    }
}

esp_err_t ieee802154_receiver_init(uint8_t channel) {
    ESP_LOGI(TAG, "Initializing IEEE 802.15.4 receiver on channel %u", channel);

    esp_err_t ret = ieee802154_init(channel, 0xABCD, 0x0002);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize IEEE 802.15.4: 0x%x", ret);
        return ret;
    }

    ieee802154_set_rx_callback(ieee802154_rx_callback);
    ESP_LOGI(TAG, "IEEE 802.15.4 receiver ready on channel %u", channel);

    return ESP_OK;
}

void ieee802154_receiver_deinit(void) {
    ieee802154_deinit();
    ESP_LOGI(TAG, "IEEE 802.15.4 receiver deinit");
}
