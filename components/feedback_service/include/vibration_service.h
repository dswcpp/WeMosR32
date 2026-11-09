#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VIBRATION_PATTERN_NONE = 0,
    /* Water: two 220 ms pulses separated by 180 ms. */
    VIBRATION_PATTERN_WATER_REMINDER,
    /* Sedentary: two 300 ms pulses separated by 200 ms. */
    VIBRATION_PATTERN_SEDENTARY_ALERT,
    /* Escalation: three 350 ms pulses separated by 180 ms. */
    VIBRATION_PATTERN_SEDENTARY_ESCALATED,
    /* Confirmation: one short 100 ms pulse. */
    VIBRATION_PATTERN_CONFIRM,
} vibration_pattern_t;

typedef struct {
    bool enabled;
    int gpio;
    bool active_high;
} vibration_service_config_t;

typedef struct {
    bool ready;
    bool active;
    bool vibrating;
    vibration_pattern_t pattern;
    uint8_t pulse_count;
    uint8_t pulse_index;
    uint32_t remaining_ms;
    uint32_t play_count;
} vibration_service_snapshot_t;

esp_err_t vibration_service_start(const vibration_service_config_t *config);
esp_err_t vibration_service_play(vibration_pattern_t pattern);
void vibration_service_stop(void);
bool vibration_service_is_ready(void);
bool vibration_service_get_snapshot(vibration_service_snapshot_t *snapshot);
uint8_t vibration_service_pattern_pulse_count(vibration_pattern_t pattern);
uint32_t vibration_service_pattern_duration_ms(vibration_pattern_t pattern);

#ifdef __cplusplus
}
#endif
