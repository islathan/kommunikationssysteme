idf_build_get_property(target IDF_TARGET)
set(priv_requires "")

if(${target} STREQUAL "linux")
    list(APPEND priv_requires nvs_flash esp_event esp_netif lwip)
endif()

idf_component_register(
    SRCS "wifi.c" "udp_client.c"
    INCLUDE_DIRS "." 
    REQUIRES button nvs_flash esp_wifi esp_event esp_netif lwip driver
)
