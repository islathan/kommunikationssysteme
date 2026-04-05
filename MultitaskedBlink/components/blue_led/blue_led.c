#include "blue_led.h"

void blue_led_init(void) {
    gpio_config_t io_conf = {.pin_bit_mask = (1ULL << BLUE_LED),
                             .mode = GPIO_MODE_OUTPUT,
                             .pull_up_en = GPIO_PULLUP_DISABLE,
                             .pull_down_en = GPIO_PULLDOWN_DISABLE,
                             .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io_conf);
    gpio_set_level(BLUE_LED, 0);
}

void toggle_blue_led(void) {
    static bool state = false;
    state = !state;
    gpio_set_level(BLUE_LED, state);
}