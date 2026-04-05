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
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "led_controller.h"
#include "sdkconfig.h"
#include <stdio.h>

#define BLINK_GPIO CONFIG_BLINK_GPIO
#define LED_TASK_PRIORITY 3
#define LED_TASK_STACKSIZE 2048

// Button event structure for queue
typedef struct {
    bool pressed;       // true = pressed, false = released
    uint8_t button_id;  // button identifier
    uint32_t timestamp; // timestamp of event
} button_event_t;

uint32_t blink_period = CONFIG_BLINK_PERIOD;
static uint8_t r = 0, g = 0, b = 0;

led_strip_handle_t led_strip;
SemaphoreHandle_t led_mutex;
SemaphoreHandle_t period_mutex;
QueueHandle_t button_event_queue;

void led_task_red(void* arg) {
    while (1) {
        xSemaphoreTake(led_mutex, portMAX_DELAY);
        r = !r;
        set_led_color(led_strip, r * 255, g * 255, b * 255);
        xSemaphoreGive(led_mutex);
        vTaskDelay(pdMS_TO_TICKS(blink_period / 2));
    }
}

void led_task_green(void* pBlinkPeriod_ms) {
    while (1) {
        xSemaphoreTake(led_mutex, portMAX_DELAY);
        g = !g;
        set_led_color(led_strip, r * 255, g * 255, b * 255);
        xSemaphoreGive(led_mutex);
        vTaskDelay(pdMS_TO_TICKS(blink_period / 2));
    }
}

void led_task_blue(void* pBlinkPeriod_ms) {
    while (1) {
        xSemaphoreTake(led_mutex, portMAX_DELAY);
        b = !b;
        set_led_color(led_strip, r * 255, g * 255, b * 255);
        xSemaphoreGive(led_mutex);
        vTaskDelay(pdMS_TO_TICKS(blink_period / 2));
    }
}

void button_task(void* arg) {
    static bool last_btn_state = false;

    while (1) {
        refresh_button_value();

        if (btn_pressed != last_btn_state) { // state change
#ifdef CONFIG_BUTTON_QUEUE
            button_event_t event = {.pressed = btn_pressed,
                                    .button_id = 0, // single button
                                    .timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS};
            xQueueSend(button_event_queue, &event, portMAX_DELAY);
#else
            if (btn_pressed && !last_btn_state) { // rising edge for semaphore mode
                xSemaphoreTake(period_mutex, portMAX_DELAY);
                if (blink_period > 100) { // Minimum speed limit
                    blink_period -= 100;
                    ESP_LOGI("BUTTON", "Blink period decreased to %d ms", blink_period);
                }
                xSemaphoreGive(period_mutex);
            }
#endif
        }

        last_btn_state = btn_pressed;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

#ifdef CONFIG_BUTTON_QUEUE
void button_handler_task(void* arg) {
    button_event_t event;

    while (1) {
        if (xQueueReceive(button_event_queue, &event, portMAX_DELAY) == pdTRUE) {
            if (event.pressed) { // button pressed
                xSemaphoreTake(period_mutex, portMAX_DELAY);
                if (blink_period > 100) { // Minimum speed limit
                    blink_period -= 100;
                    ESP_LOGI("BUTTON", "Blink period decreased to %d ms (timestamp: %d)",
                             blink_period, event.timestamp);
                }
                xSemaphoreGive(period_mutex);
            }
            // Could handle button release events here if needed
        }
    }
}
#endif

void app_main(void) {
    led_strip = configure_led(BLINK_GPIO);
    configure_button();

    led_mutex = xSemaphoreCreateMutex();
    period_mutex = xSemaphoreCreateMutex();

#ifdef CONFIG_BUTTON_QUEUE
    button_event_queue = xQueueCreate(10, sizeof(button_event_t));
    xTaskCreate(button_handler_task, "BTN_HANDLER", LED_TASK_STACKSIZE, NULL, 3, NULL);
#endif

    xTaskCreate(led_task_red, "LED_R", LED_TASK_STACKSIZE, &blink_period, 3, NULL);
    xTaskCreate(led_task_green, "LED_G", LED_TASK_STACKSIZE, &blink_period, 3, NULL);
    xTaskCreate(led_task_blue, "LED_B", LED_TASK_STACKSIZE, &blink_period, 3, NULL);

    xTaskCreate(button_task, "BTN", LED_TASK_STACKSIZE, NULL, 3, NULL);
}
