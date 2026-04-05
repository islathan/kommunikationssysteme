idf_component_register(SRCS "led_controller.c" "blink_example_main.c"
                       INCLUDE_DIRS "."
                       REQUIRES button blue_led)
