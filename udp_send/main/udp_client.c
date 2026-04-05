#include "button.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"
#include "wifi.h"

#define BUTTON_TASK_STACKSIZE 2048

// // we should send something like
// enum cmd {
//     cmd_press
// }

// struct cmdPressPayLoad {
//     bool pressed
//     uint32_t timestamp
// }

// struct msg {
//     version  <- for the FUTURE!
//     sequencenr
//     enumcmd cmd;
//     union {
//         cmdPressPayload
//     }
// }

typedef struct {
    bool pressed;
    uint32_t timestamp;
} button_event_t;

static const char* TAG = "Main";

SemaphoreHandle_t mutex;
QueueHandle_t button_event_queue;

void button_task(void* arg) {
    static bool last_btn_state = false;

    while (1) {
        refresh_button_value();

        if (btn_pressed != last_btn_state) { // state change
            button_event_t event = {.pressed = btn_pressed,
                                    .timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS};
            xQueueSend(button_event_queue, &event, portMAX_DELAY);
        }

        last_btn_state = btn_pressed;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void button_handler_task(void* arg) {
    button_event_t event;

    // --- socket setup
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

    struct sockaddr_in dest_addr = {0};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(CONFIG_BROADCAST_PORT);
    dest_addr.sin_addr.s_addr = broadcast;

    ESP_LOGI(TAG, "UDP ready, sending to %s:%d", inet_ntoa(dest_addr.sin_addr),
             ntohs(dest_addr.sin_port));

    while (1) {
        if (xQueueReceive(button_event_queue, &event, portMAX_DELAY) == pdTRUE) {
            if (event.pressed) {
                ESP_LOGI(TAG, "Button pressed at %lums, sending UDP...", event.timestamp);

                int err = sendto(sock, &event, sizeof(event), 0, (struct sockaddr*)&dest_addr,
                                 sizeof(dest_addr));
                if (err < 0) {
                    ESP_LOGE(TAG, "sendto failed: errno %d", errno);
                }
            }
        }
    }

    close(sock);
}

void app_main(void) {
    // Initialize NVS
    ESP_ERROR_CHECK(nvs_flash_init());

    // Initialize Wi-Fi
    EventGroupHandle_t wifi_event_group = wifi_init();

    ESP_LOGI(TAG, "Waiting for Wi-Fi connection...");

    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    ESP_LOGI(TAG, "Wi-Fi connected.");

    mutex = xSemaphoreCreateMutex();
    button_event_queue = xQueueCreate(10, sizeof(button_event_t));
    xTaskCreate(button_handler_task, "BTN_HANDLER", BUTTON_TASK_STACKSIZE, NULL, 3, NULL);
    xTaskCreate(button_task, "BTN_POLL", BUTTON_TASK_STACKSIZE, NULL, 3, NULL);
}
