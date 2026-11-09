#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CARE_DEFAULT_WATER_GOAL_ML 1800u
#define CARE_DEFAULT_CUP_ML 250u
#define CARE_DEFAULT_WORK_START_MIN (9u * 60u)
#define CARE_DEFAULT_MORNING_END_MIN (12u * 60u)
#define CARE_DEFAULT_AFTERNOON_START_MIN (13u * 60u + 30u)
#define CARE_DEFAULT_WORK_END_MIN (18u * 60u)
#define CARE_DEFAULT_OVERTIME_END_MIN (21u * 60u)

typedef enum {
    CARE_ALERT_MODE_BOTH = 0,
    CARE_ALERT_MODE_VIBRATE,
    CARE_ALERT_MODE_SOUND,
    CARE_ALERT_MODE_SILENT,
} care_alert_mode_t;

typedef struct {
    bool water_enabled;
    uint16_t daily_goal_ml;
    uint16_t cup_ml;
    uint16_t work_start_min;
    uint16_t morning_end_min;
    uint16_t afternoon_start_min;
    uint16_t work_end_min;
    bool overtime_enabled;
    uint16_t overtime_end_min;
    care_alert_mode_t alert_mode;
} care_settings_t;

typedef struct {
    care_settings_t settings;
    bool time_valid;
    bool within_work;
    bool overtime_active;
    bool water_due;
    uint32_t today_sitting_seconds;
    uint32_t longest_sitting_seconds;
    uint32_t current_sitting_seconds;
    uint32_t completed_breaks;
    uint32_t gentle_nudges;
    uint32_t activity_nudges;
    uint32_t water_ml;
    uint32_t water_goal_ml;
    uint32_t water_remaining_ml;
    uint32_t next_water_seconds;
    uint8_t scheduled_water_cups;
    uint8_t completed_water_cups;
    uint8_t care_level;
    char care_action[24];
} care_snapshot_t;

typedef enum {
    CARE_EVENT_NONE = 0,
    CARE_EVENT_WATER_REMINDER,
} care_event_t;

esp_err_t care_guidance_init(void);
esp_err_t care_guidance_set_settings(const care_settings_t *settings);
esp_err_t care_guidance_add_water(int32_t delta_ml);
void care_guidance_get_snapshot(care_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif
