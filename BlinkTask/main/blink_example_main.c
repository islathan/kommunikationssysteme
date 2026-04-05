/*
Author: Elias Sohm
Date: 26.02.2026
Copyright (c) 2026 Elias Sohm
All rights reserved.
*/
#include "button.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "sdkconfig.h"
#include <stdio.h>

#define BLINK_GPIO CONFIG_BLINK_GPIO
#define BTN_GPIO GPIO_NUM_9
#define LED_TASK_PRIORITY 3
#define LED_TASK_STACKSIZE 2048

static const char* TAG = "Main";
static led_strip_handle_t led_strip;
TaskHandle_t gLedTaskHandle = NULL;

static void configure_led(void) {
    ESP_LOGI(TAG, "Example configured to blink addressable LED!");

    // Enable RGB LED power (GPIO19 HIGH)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << 19),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(19, 1);

    vTaskDelay(10 / portTICK_PERIOD_MS);

    /* LED strip initialization with the GPIO and pixels number*/
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = 1, // at least one LED on board
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };

    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));

    /* Set all LED off to clear all pixels */
    led_strip_clear(led_strip);
    led_strip_refresh(led_strip);
}

void blink_task(void* pBlinkPeriod_ms) {
    uint8_t s_led_state = 0;
    while (1) {
        ESP_LOGI(TAG, "Turning the LED %s!", s_led_state == true ? "ON" : "OFF");
        if (s_led_state) {
            /* Set the LED pixel using RGB from 0 (0%) to 255 (100%) for each color */
            led_strip_set_pixel(led_strip, 0, 25, 25, 25);
            /* Refresh the strip to send data */
            led_strip_refresh(led_strip);
        } else {
            /* Set all LED off to clear all pixels */
            led_strip_clear(led_strip);
        }
        s_led_state = !s_led_state;
        vTaskDelay(*(uint32_t*)pBlinkPeriod_ms / (portTICK_PERIOD_MS * 2));
    }
}

void app_main(void) {
    configure_led();
    configure_button();

    static uint32_t blink_period = CONFIG_BLINK_PERIOD;

    xTaskCreate(blink_task, "BLINK", LED_TASK_STACKSIZE, &blink_period, LED_TASK_PRIORITY,
                &gLedTaskHandle);
    vTaskSuspend(gLedTaskHandle);

    static bool last_btn_state = false;

    while (1) {
        refresh_button_value();

        if (btn_pressed && !last_btn_state) { // rising edge
            eTaskState state = eTaskGetState(gLedTaskHandle);

            if (state == eSuspended) {
                vTaskResume(gLedTaskHandle);
            } else {
                vTaskSuspend(gLedTaskHandle);
            }
        }

        last_btn_state = btn_pressed;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
