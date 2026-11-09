idf_component_register(
    SRCS
        "feedback_player.c"
        "buzzer_service.c"
        "vibration_service.c"
        "feedback_service.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_driver_gpio
        esp_timer
        freertos
)
