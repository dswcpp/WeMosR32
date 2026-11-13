#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "wifi_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool enabled;
    int pir_gpio;
    int sht30_uart_port;
    int sht30_tx_gpio;
    int sht30_rx_gpio;
    int oled_i2c_port;
    int oled_sda_gpio;
    int oled_scl_gpio;
    uint32_t oled_i2c_hz;
    bool oled_sh1106;
    bool buzzer_enabled;
    int buzzer_gpio;
    bool vibration_enabled;
    int vibration_gpio;
    bool rgb_led_enabled;
    int rgb_red_gpio;
    int rgb_green_gpio;
    int rgb_blue_gpio;
    bool buttons_enabled;
    int button_gpios[4];
    uint32_t button_debounce_ms;
    uint32_t button_long_press_ms;
    uint32_t sedentary_initial_ms;
    uint32_t sedentary_escalation_ms;
    uint32_t sedentary_break_ms;
    uint32_t sedentary_absence_ms;
} app_wellness_config_t;

typedef struct {
    bool enabled;
    int power_gpio;
    int brightness_gpio;
    int color_gpio;
    bool active_low;
    uint32_t pulse_ms;
    uint32_t settle_ms;
    bool presence_auto;
    bool presence_led_enabled;
    int presence_led_gpio;
    bool presence_led_active_low;
} app_workstation_light_config_t;

typedef struct {
    const char *device_name;
    const char *version;
    bool http_server_enabled;
    wifi_manager_config_t wifi;
    app_wellness_config_t wellness;
    app_workstation_light_config_t workstation_light;
} app_config_t;

const app_config_t *app_config_get(void);

#ifdef __cplusplus
}
#endif
