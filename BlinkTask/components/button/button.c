#include "button.h"

uint8_t btn_pressed;

static uint8_t debounced_read(gpio_num_t pin) {
    int sum = 0;
    for (int i = 0; i < 5; i++) {
        sum += gpio_get_level(BTN_GPIO);
        vTaskDelay(1);
    }
    return (sum < 3); // active low button
}

void refresh_button_value(void) { btn_pressed = debounced_read(BTN_GPIO); }

void configure_button() {
    gpio_config_t btn_config = {
        .pin_bit_mask = (1ULL << BTN_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = true,
        .pull_down_en = false,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_config);
}