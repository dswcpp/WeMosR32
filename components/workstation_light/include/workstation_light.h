#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*workstation_light_presence_provider_t)(
    bool *present, void *context);

typedef enum {
    WORKSTATION_LIGHT_ACTION_POWER_OFF = 0,
    WORKSTATION_LIGHT_ACTION_POWER_ON,
    WORKSTATION_LIGHT_ACTION_TOGGLE_POWER,
    WORKSTATION_LIGHT_ACTION_BRIGHTNESS_NEXT,
    WORKSTATION_LIGHT_ACTION_COLOR_NEXT,
} workstation_light_action_t;

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
    workstation_light_presence_provider_t presence_provider;
    void *presence_context;
} workstation_light_config_t;

typedef struct {
    bool enabled;
    bool ready;
    bool power_on;
    bool busy;
    bool active_low;
    uint32_t pulse_ms;
    uint32_t settle_ms;
    uint32_t brightness_cycles;
    uint32_t color_cycles;
    workstation_light_action_t last_action;
    bool last_action_automatic;
    bool presence_auto;
    bool presence_known;
    bool presence;
    bool presence_led_enabled;
    int presence_led_gpio;
    bool presence_led_on;
} workstation_light_snapshot_t;

esp_err_t workstation_light_init(
    const workstation_light_config_t *config);
esp_err_t workstation_light_execute(workstation_light_action_t action);
bool workstation_light_get_snapshot(workstation_light_snapshot_t *snapshot);
const char *workstation_light_action_name(workstation_light_action_t action);

#ifdef __cplusplus
}
#endif
