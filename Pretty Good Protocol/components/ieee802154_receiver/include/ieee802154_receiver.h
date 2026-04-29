#pragma once

#include "esp_err.h"
#include <stdint.h>

/**
 * Initialize IEEE 802.15.4 receiver
 * Starts listening for protobuf-encoded frames
 * @param channel Channel to listen on (11-26)
 * @return ESP_OK on success
 */
esp_err_t ieee802154_receiver_init(uint8_t channel);

/**
 * Deinitialize receiver
 */
void ieee802154_receiver_deinit(void);
