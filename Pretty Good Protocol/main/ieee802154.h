#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

// IEEE 802.15.4 Frame Control Field bits
#define IEEE802154_FRAME_TYPE_DATA (0x01)
#define IEEE802154_FRAME_TYPE_ACK (0x02)
#define IEEE802154_FRAME_TYPE_BEACON (0x00)

// Addressing modes
#define IEEE802154_ADDR_MODE_NONE (0x00)
#define IEEE802154_ADDR_MODE_SHORT (0x02)
#define IEEE802154_ADDR_MODE_EXTENDED (0x03)

// Frame control field flags
#define IEEE802154_INTRA_PAN (0x40)     // Bit 6
#define IEEE802154_ACK_REQUEST (0x20)   // Bit 5
#define IEEE802154_FRAME_PENDING (0x10) // Bit 4

typedef struct {
    uint16_t frame_control; // Frame control field
    uint8_t sequence;       // Sequence number
    uint16_t dest_pan_id;   // Destination PAN ID
    uint16_t dest_addr;     // Destination short address
    uint16_t src_pan_id;    // Source PAN ID
    uint16_t src_addr;      // Source short address
    uint8_t* payload;       // Pointer to payload
    uint16_t payload_len;   // Payload length
} ieee802154_frame_t;

/**
 * Callback function for received frames
 * Called when a valid frame is received
 * @param frame Parsed frame structure
 * @param rssi Received Signal Strength Indicator (dBm)
 */
typedef void (*ieee802154_rx_callback_t)(const ieee802154_frame_t* frame, int8_t rssi);

/**
 * Initialize IEEE 802.15.4 driver
 * @param channel  Channel to use (11-26)
 * @param pan_id   PAN identifier shared by all devices in the network
 * @param own_addr Short address of this device (must be unique per device)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ieee802154_init(uint8_t channel, uint16_t pan_id, uint16_t own_addr);

/**
 * Deinitialize IEEE 802.15.4 driver
 */
void ieee802154_deinit(void);

/**
 * Build an IEEE 802.15.4 frame according to standard
 * Builds frame with short addressing mode
 * @param frame Frame structure to fill
 * @param sequence Sequence number
 * @param dest_pan_id Destination PAN ID
 * @param dest_addr Destination short address
 * @param src_pan_id Source PAN ID
 * @param src_addr Source short address
 * @param payload Payload data
 * @param payload_len Payload length
 * @param out Output buffer for complete frame
 * @param out_len Output buffer size
 * @return Number of bytes written to out, -1 on error
 */
int ieee802154_build_frame(uint8_t sequence, uint16_t dest_pan_id, uint16_t dest_addr,
                           uint16_t src_pan_id, uint16_t src_addr, const uint8_t* payload,
                           uint16_t payload_len, uint8_t* out, size_t out_len);

/**
 * Send IEEE 802.15.4 frame
 * @param frame_data Frame data (including FHR + PHR)
 * @param frame_len Frame length
 * @return ESP_OK on success
 */
esp_err_t ieee802154_send(const uint8_t* frame_data, uint16_t frame_len);

/**
 * Set the 802.15.4 channel
 * @param channel Channel number (11-26)
 * @return ESP_OK on success
 */
esp_err_t ieee802154_set_channel(uint8_t channel);

/**
 * Enable receiver
 */
esp_err_t ieee802154_rx_enable(void);

/**
 * Disable receiver
 */
void ieee802154_rx_disable(void);

/**
 * Register receive callback function
 * Called whenever a valid frame is received
 * @param callback Callback function pointer
 */
void ieee802154_set_rx_callback(ieee802154_rx_callback_t callback);

/**
 * Parse received IEEE 802.15.4 frame
 * Extracts header information from raw frame data
 * @param frame_data Raw frame data (with length byte as first byte)
 * @param frame_len Frame length (including length byte)
 * @param out_frame Output parsed frame structure
 * @return 0 on success, -1 on error
 */
int ieee802154_parse_frame(const uint8_t* frame_data, uint16_t frame_len,
                           ieee802154_frame_t* out_frame);
