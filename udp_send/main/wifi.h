#ifndef WIFI_H
#define WIFI_H

#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define WIFI_CONNECTED_BIT BIT0

EventGroupHandle_t wifi_init(void);

#endif
