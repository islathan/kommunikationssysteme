idf_component_register(SRCS "receiver.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_ieee802154 protobuf-c)
