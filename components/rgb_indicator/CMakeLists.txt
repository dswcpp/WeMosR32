idf_component_register(
    SRCS
        "rgb_indicator.c"
        "rgb_indicator_engine.c"
    INCLUDE_DIRS "include"
    REQUIRES
        app_events
        esp_driver_gpio
        esp_driver_ledc
        esp_timer
        freertos
)
