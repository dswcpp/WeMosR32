idf_component_register(
    SRCS "rgb_indicator.c"
    INCLUDE_DIRS "include"
    REQUIRES
        config_service
        esp_driver_gpio
        esp_driver_ledc
        freertos
)
