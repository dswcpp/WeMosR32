idf_component_register(
    SRCS "workstation_light.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_driver_gpio
        freertos
)
