#ifndef BLUE_LED_H
#define BLUE_LED_H

#include "driver/gpio.h"
#include <stdio.h>

#define BLUE_LED GPIO_NUM_7

void blue_led_init(void);
void toggle_blue_led(void);

#endif