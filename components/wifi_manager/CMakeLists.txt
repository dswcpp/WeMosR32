idf_component_register(
    SRCS "wifi_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES
        app_events
        config_service
        esp_event
        esp_hw_support
        esp_netif
        esp_timer
        esp_wifi
)
