#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool pir_present;
    bool pir_motion;
    uint32_t pir_motion_events;

    bool sht30_valid;
    bool sht30_online;
    int16_t temperature_tenths_c;
    uint16_t humidity_tenths_rh;
    uint32_t sht30_age_ms;
    uint32_t sht30_frames;
    uint32_t sht30_bad_frames;

    bool oled_ready;
    uint8_t oled_address;

    bool sedentary_active;
    bool sedentary_present;
    bool sedentary_alert_active;
    /* Stable phase name owned by wellness_monitor. */
    const char *sedentary_phase;
    bool beeping;
    bool vibrating;
    uint8_t beep_count;
    uint32_t sitting_elapsed_ms;
    uint32_t next_reminder_ms;
    uint32_t break_remaining_ms;
    uint32_t alert_starts;
    uint32_t escalations;
    uint32_t gentle_nudges;
    uint32_t activity_nudges;
    uint32_t short_breaks;
    uint32_t completed_breaks;
    uint8_t reminder_level;
} wellness_snapshot_t;

esp_err_t wellness_monitor_start(void);
bool wellness_monitor_get_snapshot(wellness_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif
