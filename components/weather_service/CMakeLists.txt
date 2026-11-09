idf_component_register(
    SRCS "weather_service.c"
    INCLUDE_DIRS "include"
    REQUIRES
        config_service
        esp-tls
        esp_http_client
        esp_timer
        freertos
        json
        time_service
        wifi_manager
)
