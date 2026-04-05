#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include "driver/gpio.h"
#include "esp_log.h"
#include "inttypes.h"
#include "led_strip.h"

led_strip_handle_t configure_led(gpio_num_t blink_gpio);
void set_led_color(led_strip_handle_t led_strip, uint32_t r, uint32_t g, uint32_t b);

#endif