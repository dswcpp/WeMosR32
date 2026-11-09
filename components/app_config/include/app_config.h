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
    bool buzzer_enabled;
    int buzzer_gpio;
    bool vibration_enabled;
    int vibration_gpio;
    bool rgb_led_enabled;
    int rgb_red_gpio;
    int rgb_green_gpio;
    int rgb_blue_gpio;
    uint32_t sedentary_initial_ms;
    uint32_t sedentary_escalation_ms;
    uint32_t sedentary_break_ms;
    uint32_t sedentary_absence_ms;
} app_wellness_config_t;

typedef struct {
    const char *device_name;
    const char *version;
    bool http_server_enabled;
    wifi_manager_config_t wifi;
    app_wellness_config_t wellness;
} app_config_t;

const app_config_t *app_config_get(void);

#ifdef __cplusplus
}
#endif
