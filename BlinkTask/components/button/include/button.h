#ifndef BUTTON_H
#define BUTTON_H

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BTN_GPIO GPIO_NUM_9

void configure_button(void);
void refresh_button_value(void);

extern uint8_t btn_pressed;

#endif