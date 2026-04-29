#include "ieee802154.h"
#include "esp_ieee802154.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "string.h"

static const char* TAG = "IEEE802154";

// Global RX callback
static ieee802154_rx_callback_t g_rx_callback = NULL;

// External RX queue from main.c
extern QueueHandle_t ieee802154_rx_queue;

typedef struct {
    uint8_t data[256];
    uint16_t len;
} ieee802154_rx_frame_t;

// Frame Control Field construction for short addresses, data frame
// Bit layout (little-endian):
// [1:0]   = 01 (data frame)
// [3:2]   = 10 (short addressing for source and dest)
// [4]     = 0 (no security)
// [5]     = 1 (ACK request)
// [6]     = 1 (intra-PAN - no separate source PAN)
// [7]     = 0 (no frame pending)
// [8]     = 0 (no AR)
// [9]     = 0 (no IE present)
// [10]    = 0 (no dest address mode extension)
// [11:14] = 0 (frame version 2006)
// [15]    = 0 (reserved)

static uint16_t build_frame_control(void) {
    uint16_t fc = 0;

    // Frame type: Data (01)
    fc |= (IEEE802154_FRAME_TYPE_DATA & 0x03);

    // Destination addressing mode: short (10)
    fc |= ((IEEE802154_ADDR_MODE_SHORT & 0x03) << 2);

    // Frame version: 2006/2015 (00)
    // Bit 11-12 already 0

    // Source addressing mode: short (10)
    fc |= ((IEEE802154_ADDR_MODE_SHORT & 0x03) << 14);

    // Intra-PAN flag (source and dest in same PAN)
    fc |= IEEE802154_INTRA_PAN;

    // ACK request
    fc |= IEEE802154_ACK_REQUEST;

    return fc;
}

esp_err_t ieee802154_init(uint8_t channel) {
    ESP_LOGI(TAG, "Initializing IEEE 802.15.4");

    ESP_ERROR_CHECK(esp_ieee802154_enable());
    ESP_ERROR_CHECK(esp_ieee802154_set_channel(channel));
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
    return esp_ieee802154_set_rx_when_idle(true);
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
        return;
    }

    // Get frame length from first byte
    uint16_t frame_len = frame[0] + 1; // +1 for the length byte itself

    // Only queue valid frames
    if (frame_len <= sizeof(ieee802154_rx_frame_t)) {
        ieee802154_rx_frame_t rx_frame;
        rx_frame.len = frame_len;
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
