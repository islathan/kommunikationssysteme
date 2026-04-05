#include "led_controller.h"

static const char* TAG = "LED Controller";
static uint8_t s_led_state = 0;

led_strip_handle_t configure_led(gpio_num_t blink_gpio) {
    led_strip_handle_t led_strip;

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
        .strip_gpio_num = blink_gpio,
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

    return led_strip;
}

void set_led_color(led_strip_handle_t led_strip, uint32_t r, uint32_t g, uint32_t b) {
    led_strip_set_pixel(led_strip, 0, r, g, b);
    led_strip_refresh(led_strip);
}