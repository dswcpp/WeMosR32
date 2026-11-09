#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BUZZER_PATTERN_NONE = 0,
    /* Water: two soft 120 ms beeps separated by 200 ms. */
    BUZZER_PATTERN_WATER_REMINDER,
    /* Sedentary: two firm 160 ms beeps separated by 220 ms. */
    BUZZER_PATTERN_SEDENTARY_ALERT,
    /* Escalation: three firm 160 ms beeps separated by 180 ms. */
    BUZZER_PATTERN_SEDENTARY_ESCALATED,
    /* Confirmation: one short 80 ms beep. */
    BUZZER_PATTERN_CONFIRM,
} buzzer_pattern_t;

typedef struct {
    bool enabled;
    int gpio;
    bool active_high;
} buzzer_service_config_t;

typedef struct {
    bool ready;
    bool active;
    bool beeping;
    buzzer_pattern_t pattern;
    uint8_t beep_count;
    uint8_t beep_index;
    uint32_t remaining_ms;
    uint32_t play_count;
} buzzer_service_snapshot_t;

esp_err_t buzzer_service_start(const buzzer_service_config_t *config);
esp_err_t buzzer_service_play(buzzer_pattern_t pattern);
void buzzer_service_stop(void);
bool buzzer_service_is_ready(void);
bool buzzer_service_get_snapshot(buzzer_service_snapshot_t *snapshot);
uint8_t buzzer_service_pattern_beep_count(buzzer_pattern_t pattern);
uint32_t buzzer_service_pattern_duration_ms(buzzer_pattern_t pattern);

#ifdef __cplusplus
}
#endif
