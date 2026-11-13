idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        app_events
        app_config
        config_service
        feedback_service
        http_api
        startup_service
        time_service
        weather_service
        wellness_monitor
        wifi_manager
        workstation_light
)
