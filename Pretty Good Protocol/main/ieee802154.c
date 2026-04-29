#include "ieee802154.h"
#include "esp_ieee802154.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "string.h"

static const char* TAG = "IEEE802154";

// Global RX callback
static ieee802154_rx_callback_t g_rx_callback = NULL;
static uint16_t g_own_addr = 0x0000;

// External RX queue from main.c
extern QueueHandle_t ieee802154_rx_queue;

typedef struct {
    uint8_t data[256];
    uint16_t len;
    int8_t rssi;
} ieee802154_rx_frame_t;

// IEEE 802.15.4 FCF bit positions:
// [2:0]   = frame type (001 = data)
// [3]     = security enabled
// [4]     = frame pending
// [5]     = ACK request
// [6]     = PAN ID compression (intra-PAN)
// [9:8]   = sequence number suppression / IE present
// [11:10] = destination addressing mode (10 = short)
// [13:12] = frame version
// [15:14] = source addressing mode (10 = short)

static uint16_t build_frame_control(void) {
    uint16_t fc = 0;

    fc |= (IEEE802154_FRAME_TYPE_DATA & 0x07);          // bits 2:0 — data frame
    fc |= ((IEEE802154_ADDR_MODE_SHORT & 0x03) << 10);  // bits 11:10 — dest addr mode
    fc |= ((IEEE802154_ADDR_MODE_SHORT & 0x03) << 14);  // bits 15:14 — src addr mode
    fc |= IEEE802154_INTRA_PAN;                         // bit 6 — no separate src PAN ID

    // No ACK request: broadcast frames are never ACKed; requesting ACK on broadcast
    // causes automatic hardware retransmissions when no ACK arrives.

    return fc;
}

esp_err_t ieee802154_init(uint8_t channel, uint16_t pan_id, uint16_t own_addr) {
    ESP_LOGI(TAG, "Initializing IEEE 802.15.4 ch=%d PAN=0x%04X addr=0x%04X",
             channel, pan_id, own_addr);

    g_own_addr = own_addr;

    // dBm, valid range: -24 to 20 on ESP32-H2/C6
    esp_ieee802154_set_txpower(20);

    ESP_ERROR_CHECK(esp_ieee802154_enable());
    ESP_ERROR_CHECK(esp_ieee802154_set_channel(channel));
    // Required for hardware address filtering — without these the radio accepts
    // everything (including own transmitted frames) or nothing at all.
    ESP_ERROR_CHECK(esp_ieee802154_set_panid(pan_id));
    ESP_ERROR_CHECK(esp_ieee802154_set_short_address(own_addr));
    // Disable promiscuous mode so the hardware filter drops frames from foreign PAN IDs.
    // The radio defaults to promiscuous ON, which is why you receive classmates' traffic.
    ESP_ERROR_CHECK(esp_ieee802154_set_promiscuous(false));
    return ESP_OK;
}

void ieee802154_deinit(void) {
    esp_ieee802154_disable();
    ESP_LOGI(TAG, "IEEE 802.15.4 disabled");
}

int ieee802154_build_frame(uint8_t sequence, uint16_t dest_pan_id, uint16_t dest_addr,
                           uint16_t src_pan_id, uint16_t src_addr, const uint8_t* payload,
                           uint16_t payload_len, uint8_t* out, size_t out_len) {
    if (!out || out_len < 12) { // Minimum frame size: 1 (len) + 2 (FC) + 1 (seq) + 2 (dest_pan) + 2
                                // (dest_addr) + 2 (src_addr)
        ESP_LOGE(TAG, "Output buffer too small");
        return -1;
    }

    uint8_t* p = out + 1; // Start after length byte
    size_t remaining = out_len - 1;

    // Frame Control (2 bytes, little-endian)
    uint16_t fc = build_frame_control();
    if (remaining < 2)
        return -1;
    *p++ = (uint8_t)(fc & 0xFF);
    *p++ = (uint8_t)((fc >> 8) & 0xFF);
    remaining -= 2;

    // Sequence Number (1 byte)
    if (remaining < 1)
        return -1;
    *p++ = sequence;
    remaining -= 1;

    // Destination PAN ID (2 bytes, little-endian)
    if (remaining < 2)
        return -1;
    *p++ = (uint8_t)(dest_pan_id & 0xFF);
    *p++ = (uint8_t)((dest_pan_id >> 8) & 0xFF);
    remaining -= 2;

    // Destination Address (2 bytes, little-endian)
    if (remaining < 2)
        return -1;
    *p++ = (uint8_t)(dest_addr & 0xFF);
    *p++ = (uint8_t)((dest_addr >> 8) & 0xFF);
    remaining -= 2;

    // Source Address (2 bytes, little-endian)
    // Note: Intra-PAN flag set, so source PAN ID is omitted
    if (remaining < 2)
        return -1;
    *p++ = (uint8_t)(src_addr & 0xFF);
    *p++ = (uint8_t)((src_addr >> 8) & 0xFF);
    remaining -= 2;

    // Payload
    if (payload_len > 0) {
        if (remaining < payload_len) {
            ESP_LOGE(TAG, "Payload too large for buffer");
            return -1;
        }
        memcpy(p, payload, payload_len);
        p += payload_len;
        remaining -= payload_len;
    }

    // Calculate frame length (excluding the length byte itself)
    int frame_len_without_length_byte = (int)(p - (out + 1));

    // Set the length byte (does NOT include the length byte itself, but includes FCS that hardware
    // will add)
    out[0] = (uint8_t)(frame_len_without_length_byte + 2); // +2 for FCS that hardware will add

    int total_frame_len = 1 + frame_len_without_length_byte; // Include length byte in total
    ESP_LOGI(TAG, "Frame built: FC=0x%04X, SEQ=%u, len=%d bytes (incl. length byte)", fc, sequence,
             total_frame_len);

    return total_frame_len;
}

esp_err_t ieee802154_send(const uint8_t* frame_data, uint16_t frame_len) {
    if (!frame_data || frame_len == 0) {
        ESP_LOGE(TAG, "Invalid frame data");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "Sending frame of %d bytes", frame_len);

    // esp_ieee802154_transmit expects the frame without FCS
    // FCS is handled by hardware
    esp_err_t ret = esp_ieee802154_transmit((uint8_t*)frame_data, true);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Frame transmitted successfully");
    } else {
        ESP_LOGE(TAG, "Failed to transmit frame: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t ieee802154_set_channel(uint8_t channel) {
    if (channel < 11 || channel > 26) {
        ESP_LOGE(TAG, "Invalid channel %d (valid range: 11-26)", channel);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = esp_ieee802154_set_channel(channel);
    return ret;
}

esp_err_t ieee802154_rx_enable(void) {
    ESP_LOGI(TAG, "Enabling receiver");
    // set_rx_when_idle only configures auto-RX after TX — it does not start receiving now.
    // esp_ieee802154_receive() is required to put the radio into RX mode immediately.
    esp_err_t ret = esp_ieee802154_set_rx_when_idle(true);
    if (ret != ESP_OK) return ret;
    return esp_ieee802154_receive();
}

void ieee802154_rx_disable(void) {
    esp_ieee802154_set_rx_when_idle(false);
    ESP_LOGI(TAG, "Receiver disabled");
}

void ieee802154_set_rx_callback(ieee802154_rx_callback_t callback) {
    g_rx_callback = callback;
    if (callback) {
        ESP_LOGI(TAG, "RX callback registered");
    } else {
        ESP_LOGI(TAG, "RX callback unregistered");
    }
}

int ieee802154_parse_frame(const uint8_t* frame_data, uint16_t frame_len,
                           ieee802154_frame_t* out_frame) {
    if (!frame_data || !out_frame || frame_len < 11) {
        ESP_LOGE(TAG, "Invalid frame or too short");
        return -1;
    }

    memset(out_frame, 0, sizeof(ieee802154_frame_t));

    // Skip length byte (frame_data[0])
    const uint8_t* p = frame_data + 1;
    size_t remaining = frame_len - 1;

    // Frame Control (2 bytes, little-endian)
    if (remaining < 2)
        return -1;
    uint16_t fc = (*p) | (*(p + 1) << 8);
    out_frame->frame_control = fc;
    p += 2;
    remaining -= 2;

    // Sequence Number (1 byte)
    if (remaining < 1)
        return -1;
    out_frame->sequence = *p;
    p += 1;
    remaining -= 1;

    // Destination PAN ID (2 bytes, little-endian)
    if (remaining < 2)
        return -1;
    out_frame->dest_pan_id = (*p) | (*(p + 1) << 8);
    p += 2;
    remaining -= 2;

    // Destination Address (2 bytes, little-endian)
    if (remaining < 2)
        return -1;
    out_frame->dest_addr = (*p) | (*(p + 1) << 8);
    p += 2;
    remaining -= 2;

    // Source Address (2 bytes, little-endian)
    // Note: Intra-PAN flag set, so source PAN ID is omitted (same as dest PAN ID)
    if (remaining < 2)
        return -1;
    out_frame->src_addr = (*p) | (*(p + 1) << 8);
    p += 2;
    remaining -= 2;

    // Source PAN ID is same as dest when intra-PAN flag is set
    out_frame->src_pan_id = out_frame->dest_pan_id;

    // Remaining bytes are payload (excluding FCS which is not included in frame_data)
    if (remaining > 0) {
        out_frame->payload = (uint8_t*)p;
        out_frame->payload_len = (uint16_t)remaining;
    } else {
        out_frame->payload = NULL;
        out_frame->payload_len = 0;
    }

    ESP_LOGI(TAG,
             "Frame parsed: FC=0x%04X SEQ=%u DEST_PAN=0x%04X DEST_ADDR=0x%04X SRC_ADDR=0x%04X "
             "PayloadLen=%d",
             fc, out_frame->sequence, out_frame->dest_pan_id, out_frame->dest_addr,
             out_frame->src_addr, out_frame->payload_len);

    return 0;
}

// ---- IEEE 802.15.4 ISR Callbacks (called from radio ISR context) ----

/**
 * Callback invoked by the IEEE 802.15.4 driver when a frame is received
 * This runs in ISR context, so keep it short
 */
void esp_ieee802154_receive_done(uint8_t* frame, esp_ieee802154_frame_info_t* frame_info) {
    if (!frame || !ieee802154_rx_queue) {
        esp_ieee802154_receive_handle_done(frame);
        return;
    }

    // Frame layout (intra-PAN, short addresses):
    // [0]=length [1-2]=FCF [3]=seq [4-5]=destPAN [6-7]=destAddr [8-9]=srcAddr [10+]=payload
    // Filter out frames we sent ourselves — promiscuous hardware can echo them back.
    if (frame[0] >= 10) {
        uint16_t src_addr = frame[8] | ((uint16_t)frame[9] << 8);
        if (src_addr == g_own_addr) {
            esp_ieee802154_receive_handle_done(frame);
            return;
        }
    }

    // Get frame length from first byte
    uint16_t frame_len = frame[0] + 1; // +1 for the length byte itself

    // Only queue valid frames
    if (frame_len <= sizeof(ieee802154_rx_frame_t)) {
        ieee802154_rx_frame_t rx_frame;
        rx_frame.len = frame_len;
        rx_frame.rssi = frame_info->rssi;
        memcpy(rx_frame.data, frame, frame_len);

        // Send to queue from ISR context
        BaseType_t higher_priority_task_woken = pdFALSE;
        xQueueSendFromISR(ieee802154_rx_queue, &rx_frame, &higher_priority_task_woken);

        if (higher_priority_task_woken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    }

    // Notify the driver that we're done handling this frame
    esp_ieee802154_receive_handle_done(frame);
}
