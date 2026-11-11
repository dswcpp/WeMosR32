#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "rgb_indicator_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool enabled;
    int red_gpio;
    int green_gpio;
    int blue_gpio;
} rgb_indicator_config_t;

typedef struct {
    bool enabled;
    bool ready;
    rgb_indicator_context_t context;
    rgb_indicator_scene_t base_scene;
    rgb_indicator_scene_t scene;
    rgb_indicator_event_t active_event;
    uint32_t event_sequence;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t brightness;
    uint8_t pulse_percent;
} rgb_indicator_snapshot_t;

esp_err_t rgb_indicator_init(const rgb_indicator_config_t *config);
void rgb_indicator_set_system_state(rgb_indicator_system_state_t state);
void rgb_indicator_set_network_state(rgb_indicator_network_state_t state);
void rgb_indicator_set_stock_state(rgb_indicator_stock_state_t state,
                                   bool refreshing,
                                   bool symbols_configured);
bool rgb_indicator_notify(rgb_indicator_event_t event);
bool rgb_indicator_get_snapshot(rgb_indicator_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif
