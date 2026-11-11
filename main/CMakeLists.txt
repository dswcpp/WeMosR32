idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        app_events
        app_config
        button_service
        config_service
        feedback_service
        http_api
        rgb_indicator
        startup_service
        status_led
        stock_monitor
        stock_service
        time_service
        wifi_manager
)
