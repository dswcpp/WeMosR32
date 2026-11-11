idf_component_register(
    SRCS "button_service.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        esp_driver_gpio
        esp_timer
        freertos
)
