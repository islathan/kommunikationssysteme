/**
 * IEEE 802.15.4 Protobuf Receiver Application
 *
 * This ESP32 application listens for protobuf-encoded messages
 * sent via IEEE 802.15.4 and decodes them in real-time.
 */

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ieee802154_receiver.h"
#include "nvs_flash.h"

static const char* TAG = "Main_Receiver";

// IEEE 802.15.4 channel to listen on (default: 15)
#define IEEE802154_CHANNEL 15

void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "IEEE 802.15.4 Protobuf Receiver");
    ESP_LOGI(TAG, "========================================");

    // Initialize NVS (required for RF calibration)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize IEEE 802.15.4 receiver
    ESP_ERROR_CHECK(ieee802154_receiver_init(IEEE802154_CHANNEL));

    ESP_LOGI(TAG, "Receiver initialized. Listening on channel %d...", IEEE802154_CHANNEL);
    ESP_LOGI(TAG, "Waiting for messages...");

    // Keep the application running
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
