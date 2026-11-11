#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "wifi_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_OLED_CONTROLLER_SSD1306 = 0,
    APP_OLED_CONTROLLER_SH1106,
} app_oled_controller_t;

typedef struct {
    bool enabled;
    int oled_i2c_port;
    int oled_sda_gpio;
    int oled_scl_gpio;
    uint32_t oled_i2c_hz;
    app_oled_controller_t oled_controller;
    int button_gpio[4];
    bool button_active_low;
    bool buzzer_enabled;
    int buzzer_gpio;
    bool vibration_enabled;
    int vibration_gpio;
    bool status_led_enabled;
    int status_led_gpio;
    bool status_led_active_high;
    bool rgb_led_enabled;
    int rgb_red_gpio;
    int rgb_green_gpio;
    int rgb_blue_gpio;
} app_board_config_t;

typedef struct {
    const char *device_name;
    const char *version;
    bool http_server_enabled;
    wifi_manager_config_t wifi;
    app_board_config_t board;
} app_config_t;

const app_config_t *app_config_get(void);

#ifdef __cplusplus
}
#endif
