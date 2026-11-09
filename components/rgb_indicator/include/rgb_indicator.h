#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RGB_INDICATOR_MODE_OFF = 0,
    RGB_INDICATOR_MODE_AUTO = 1,
    RGB_INDICATOR_MODE_SOLID = 2,
    RGB_INDICATOR_MODE_RAINBOW = 3,
} rgb_indicator_mode_t;

typedef struct {
    bool enabled;
    int red_gpio;
    int green_gpio;
    int blue_gpio;
} rgb_indicator_config_t;

typedef struct {
    bool enabled;
    bool ready;
    rgb_indicator_mode_t mode;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t brightness;
    bool wellness_active;
    bool wellness_present;
    bool wellness_alert;
    bool water_due;
} rgb_indicator_snapshot_t;

esp_err_t rgb_indicator_init(const rgb_indicator_config_t *config);
esp_err_t rgb_indicator_set_mode(rgb_indicator_mode_t mode, uint8_t red,
                                 uint8_t green, uint8_t blue,
                                 uint8_t brightness);
void rgb_indicator_set_wellness_state(bool active, bool present, bool alert,
                                      bool water_due);
bool rgb_indicator_get_snapshot(rgb_indicator_snapshot_t *snapshot);
const char *rgb_indicator_mode_name(rgb_indicator_mode_t mode);

#ifdef __cplusplus
}
#endif
