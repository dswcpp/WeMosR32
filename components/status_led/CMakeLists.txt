idf_component_register(
    SRCS
        "status_led.c"
        "status_led_pattern.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_driver_gpio
        esp_timer
        freertos
)
