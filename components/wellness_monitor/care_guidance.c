#include "care_guidance_internal.h"
#include "water_reminder.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "config_service.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define CARE_SETTINGS_MAGIC 0x57434647u
#define CARE_DAILY_MAGIC 0x57434459u
#define CARE_SETTINGS_VERSION 2u
#define CARE_SETTINGS_LEGACY_VERSION 1u
#define CARE_DAILY_VERSION 1u
#define CARE_LOCAL_UTC_OFFSET_S (8 * 60 * 60)
#define CARE_PERSIST_INTERVAL_MS (10u * 60u * 1000u)
#define CARE_PERSIST_RETRY_MIN_MS (1u * 1000u)
#define CARE_PERSIST_RETRY_MAX_MS (10u * 60u * 1000u)
#define CARE_WATER_NAG_INTERVAL_S (15u * 60u)
#define CARE_WATER_MIN_GOAL_ML 500u
#define CARE_WATER_MAX_GOAL_ML 5000u
#define CARE_WATER_MIN_CUP_ML 100u
#define CARE_WATER_MAX_CUP_ML 500u
#define CARE_WATER_MAX_TOTAL_ML 10000u

static const char *TAG = "care";

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
} care_settings_v1_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    care_settings_v1_t settings;
} care_settings_blob_v1_t;

typedef struct {
    care_settings_v1_t base;
    uint8_t alert_mode;
    uint8_t reserved;
} care_settings_v2_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    care_settings_v2_t settings;
} care_settings_blob_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    int32_t date_key;
    uint32_t today_sitting_seconds;
    uint32_t longest_sitting_seconds;
    uint32_t completed_breaks;
    uint32_t gentle_nudges;
    uint32_t activity_nudges;
    uint32_t water_ml;
} care_daily_blob_t;

static SemaphoreHandle_t s_lock;
static bool s_initialized;
static care_settings_t s_settings;
static care_daily_blob_t s_daily;
static uint32_t s_sitting_fraction_ms;
static uint32_t s_last_tick_ms;
static uint32_t s_next_persist_attempt_ms;
static uint32_t s_persist_retry_ms;
static bool s_daily_dirty;
static water_reminder_t s_water_reminder;
static uint32_t s_current_sitting_seconds;
static bool s_time_valid;
static bool s_within_work;
static bool s_overtime_active;
static bool s_water_due;
static uint32_t s_next_water_seconds;
static uint8_t s_scheduled_water_cups;
static uint8_t s_completed_water_cups;
static uint8_t s_care_level;
static char s_care_action[24];

static care_settings_t default_settings(void)
{
    return (care_settings_t) {
        .water_enabled = true,
        .daily_goal_ml = CARE_DEFAULT_WATER_GOAL_ML,
        .cup_ml = CARE_DEFAULT_CUP_ML,
        .work_start_min = CARE_DEFAULT_WORK_START_MIN,
        .morning_end_min = CARE_DEFAULT_MORNING_END_MIN,
        .afternoon_start_min = CARE_DEFAULT_AFTERNOON_START_MIN,
        .work_end_min = CARE_DEFAULT_WORK_END_MIN,
        .overtime_enabled = false,
        .overtime_end_min = CARE_DEFAULT_OVERTIME_END_MIN,
        .alert_mode = CARE_ALERT_MODE_BOTH,
    };
}

static bool valid_settings(const care_settings_t *settings)
{
    if (settings == NULL ||
        settings->daily_goal_ml < CARE_WATER_MIN_GOAL_ML ||
        settings->daily_goal_ml > CARE_WATER_MAX_GOAL_ML ||
        settings->cup_ml < CARE_WATER_MIN_CUP_ML ||
        settings->cup_ml > CARE_WATER_MAX_CUP_ML ||
        settings->work_start_min >= 24u * 60u ||
        settings->morning_end_min >= 24u * 60u ||
        settings->afternoon_start_min >= 24u * 60u ||
        settings->work_end_min >= 24u * 60u ||
        settings->overtime_end_min >= 24u * 60u ||
        settings->work_start_min >= settings->morning_end_min ||
        settings->morning_end_min > settings->afternoon_start_min ||
        settings->afternoon_start_min >= settings->work_end_min ||
        (unsigned)settings->alert_mode > (unsigned)CARE_ALERT_MODE_SILENT) {
        return false;
    }

    return !settings->overtime_enabled ||
           settings->overtime_end_min > settings->work_end_min;
}

static int32_t local_date_key(int64_t unix_time)
{
    time_t local_seconds =
        (time_t)(unix_time + CARE_LOCAL_UTC_OFFSET_S);
    struct tm local = {0};

    if (gmtime_r(&local_seconds, &local) == NULL) {
        return 0;
    }
    return (int32_t)((local.tm_year + 1900) * 10000 +
                     (local.tm_mon + 1) * 100 + local.tm_mday);
}

static bool local_day_minute(int64_t unix_time, uint16_t *day_minute,
                             uint8_t *weekday)
{
    time_t local_seconds =
        (time_t)(unix_time + CARE_LOCAL_UTC_OFFSET_S);
    struct tm local = {0};

    if (unix_time <= 0 || gmtime_r(&local_seconds, &local) == NULL) {
        return false;
    }
    if (day_minute != NULL) {
        *day_minute = (uint16_t)(local.tm_hour * 60 + local.tm_min);
    }
    if (weekday != NULL) {
        *weekday = (uint8_t)local.tm_wday;
    }
    return true;
}

static uint16_t work_elapsed_minutes(const care_settings_t *settings,
                                     uint16_t day_minute)
{
    uint16_t morning_minutes =
        settings->morning_end_min - settings->work_start_min;
    uint16_t afternoon_minutes =
        settings->work_end_min - settings->afternoon_start_min;

    if (day_minute <= settings->work_start_min) {
        return 0u;
    }
    if (day_minute < settings->morning_end_min) {
        return day_minute - settings->work_start_min;
    }
    if (day_minute < settings->afternoon_start_min) {
        return morning_minutes;
    }
    if (day_minute < settings->work_end_min) {
        return morning_minutes +
               (day_minute - settings->afternoon_start_min);
    }
    return (uint16_t)(morning_minutes + afternoon_minutes);
}

static uint16_t work_target_minute(const care_settings_t *settings,
                                   uint32_t elapsed_minute)
{
    uint32_t morning_minutes =
        settings->morning_end_min - settings->work_start_min;

    if (elapsed_minute <= morning_minutes) {
        return (uint16_t)(settings->work_start_min + elapsed_minute);
    }
    return (uint16_t)(settings->afternoon_start_min +
                      (elapsed_minute - morning_minutes));
}

static uint8_t cup_count_for_settings(const care_settings_t *settings)
{
    uint32_t cups =
        (settings->daily_goal_ml + settings->cup_ml - 1u) /
        settings->cup_ml;

    if (cups == 0u) {
        cups = 1u;
    }
    if (cups > 255u) {
        cups = 255u;
    }
    return (uint8_t)cups;
}

static void reset_daily(int32_t date_key)
{
    uint32_t water_ml = s_daily.water_ml;

    memset(&s_daily, 0, sizeof(s_daily));
    s_daily.magic = CARE_DAILY_MAGIC;
    s_daily.version = CARE_DAILY_VERSION;
    s_daily.date_key = date_key;
    if (date_key == 0) {
        s_daily.water_ml = water_ml;
    }
    s_sitting_fraction_ms = 0u;
    water_reminder_reset(&s_water_reminder);
}

static esp_err_t save_settings_record(const care_settings_t *settings)
{
    care_settings_v2_t stored = {
        .base = {
            .water_enabled = settings->water_enabled,
            .daily_goal_ml = settings->daily_goal_ml,
            .cup_ml = settings->cup_ml,
            .work_start_min = settings->work_start_min,
            .morning_end_min = settings->morning_end_min,
            .afternoon_start_min = settings->afternoon_start_min,
            .work_end_min = settings->work_end_min,
            .overtime_enabled = settings->overtime_enabled,
            .overtime_end_min = settings->overtime_end_min,
        },
        .alert_mode = (uint8_t)settings->alert_mode,
    };
    care_settings_blob_t blob = {
        .magic = CARE_SETTINGS_MAGIC,
        .version = CARE_SETTINGS_VERSION,
        .settings = stored,
    };

    return config_service_save_blob(CONFIG_SERVICE_ITEM_CARE_SETTINGS, &blob,
                                    sizeof(blob));
}

static esp_err_t save_settings(void)
{
    return save_settings_record(&s_settings);
}

static esp_err_t save_daily(void)
{
    return config_service_save_blob(CONFIG_SERVICE_ITEM_CARE_DAILY, &s_daily,
                                    sizeof(s_daily));
}

static bool uptime_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static void reset_persist_backoff(void)
{
    s_persist_retry_ms = CARE_PERSIST_RETRY_MIN_MS;
}

static care_settings_t migrate_settings_v1(
    const care_settings_v1_t *legacy)
{
    care_settings_t settings = default_settings();

    settings.water_enabled = legacy->water_enabled;
    settings.daily_goal_ml = legacy->daily_goal_ml;
    settings.cup_ml = legacy->cup_ml;
    settings.work_start_min = legacy->work_start_min;
    settings.morning_end_min = legacy->morning_end_min;
    settings.afternoon_start_min = legacy->afternoon_start_min;
    settings.work_end_min = legacy->work_end_min;
    settings.overtime_enabled = legacy->overtime_enabled;
    settings.overtime_end_min = legacy->overtime_end_min;
    settings.alert_mode = CARE_ALERT_MODE_BOTH;
    return settings;
}

static care_settings_t migrate_settings_v2(
    const care_settings_v2_t *stored)
{
    care_settings_t settings = migrate_settings_v1(&stored->base);

    if (stored->alert_mode <= (uint8_t)CARE_ALERT_MODE_SILENT) {
        settings.alert_mode = (care_alert_mode_t)stored->alert_mode;
    }
    return settings;
}

static void load_persisted_state(void)
{
    care_settings_blob_t settings_blob = {0};
    care_daily_blob_t daily_blob = {0};
    size_t size = sizeof(settings_blob);
    bool found = false;

    s_settings = default_settings();
    reset_daily(0);

    if (config_service_load_blob(CONFIG_SERVICE_ITEM_CARE_SETTINGS,
                                 &settings_blob, &size, &found) == ESP_OK &&
        found &&
        size == sizeof(settings_blob) &&
        settings_blob.magic == CARE_SETTINGS_MAGIC &&
        settings_blob.version == CARE_SETTINGS_VERSION) {
        care_settings_t loaded =
            migrate_settings_v2(&settings_blob.settings);

        if (valid_settings(&loaded)) {
            s_settings = loaded;
        }
    } else if (found &&
               size == sizeof(care_settings_blob_v1_t) &&
               settings_blob.magic == CARE_SETTINGS_MAGIC &&
               settings_blob.version == CARE_SETTINGS_LEGACY_VERSION) {
        care_settings_blob_v1_t legacy_blob = {0};
        care_settings_t migrated;

        memcpy(&legacy_blob, &settings_blob, sizeof(legacy_blob));
        migrated = migrate_settings_v1(&legacy_blob.settings);
        if (valid_settings(&migrated)) {
            esp_err_t migrate_err;

            s_settings = migrated;
            migrate_err = save_settings();
            if (migrate_err == ESP_OK) {
                ESP_LOGI(TAG, "care settings migrated from v1 to v2");
            } else {
                ESP_LOGW(TAG, "failed to persist care settings migration: %s",
                         esp_err_to_name(migrate_err));
            }
        }
    }

    size = sizeof(daily_blob);
    found = false;
    if (config_service_load_blob(CONFIG_SERVICE_ITEM_CARE_DAILY, &daily_blob,
                                 &size, &found) == ESP_OK &&
        found &&
        size == sizeof(daily_blob) &&
        daily_blob.magic == CARE_DAILY_MAGIC &&
        daily_blob.version == CARE_DAILY_VERSION) {
        s_daily = daily_blob;
        if (s_daily.water_ml > CARE_WATER_MAX_TOTAL_ML) {
            s_daily.water_ml = CARE_WATER_MAX_TOTAL_ML;
        }
    }
}

static const char *care_action_for_state(
    uint8_t reminder_level, bool overtime_active, bool within_work,
    bool water_due, bool time_valid)
{
    if (reminder_level >= 3u) {
        return overtime_active ? "overtime_stand_now" : "stand_up_now";
    }
    if (reminder_level >= 2u) {
        return overtime_active ? "overtime_walk_water"
                               : "walk_or_restroom";
    }
    if (!time_valid) {
        return "sync_time";
    }
    if (water_due) {
        return "drink_water";
    }
    if (reminder_level >= 1u) {
        return "neck_break";
    }
    if (within_work) {
        return "focus_work";
    }
    if (overtime_active) {
        return "overtime_focus";
    }
    return "rest_time";
}

static void refresh_derived_state(int64_t unix_time, bool time_valid)
{
    uint16_t day_minute = 0;
    uint32_t morning_minutes =
        s_settings.morning_end_min - s_settings.work_start_min;
    uint32_t afternoon_minutes =
        s_settings.work_end_min - s_settings.afternoon_start_min;
    uint32_t effective_minutes = morning_minutes + afternoon_minutes;
    uint8_t cups = cup_count_for_settings(&s_settings);
    uint32_t elapsed_work = 0;
    uint8_t scheduled = 0;
    uint32_t next_seconds = 0;
    uint8_t reminder_level = 0;

    s_time_valid = time_valid && unix_time > 0;
    s_within_work = false;
    s_overtime_active = false;
    s_water_due = false;
    s_next_water_seconds = 0;
    s_scheduled_water_cups = 0;
    s_completed_water_cups = 0;

    if (!s_time_valid ||
        !local_day_minute(unix_time, &day_minute, NULL)) {
        s_care_level = 0;
        snprintf(s_care_action, sizeof(s_care_action), "%s", "sync_time");
        return;
    }

    s_within_work =
        (day_minute >= s_settings.work_start_min &&
         day_minute < s_settings.morning_end_min) ||
        (day_minute >= s_settings.afternoon_start_min &&
         day_minute < s_settings.work_end_min);
    s_overtime_active =
        s_settings.overtime_enabled &&
        day_minute >= s_settings.work_end_min &&
        day_minute < s_settings.overtime_end_min;
    elapsed_work = work_elapsed_minutes(&s_settings, day_minute);

    if (s_settings.water_enabled && effective_minutes > 0u) {
        for (uint8_t index = 1u; index <= cups; index++) {
            uint32_t threshold =
                ((uint32_t)index * effective_minutes) / (cups + 1u);

            if (elapsed_work >= threshold) {
                scheduled++;
            } else {
                uint16_t target_minute =
                    work_target_minute(&s_settings, threshold);
                if (target_minute > day_minute) {
                    next_seconds =
                        (uint32_t)(target_minute - day_minute) * 60u;
                }
                break;
            }
        }
        s_scheduled_water_cups = scheduled;
        s_completed_water_cups =
            (uint8_t)(s_daily.water_ml / s_settings.cup_ml);
        s_water_due =
            s_within_work &&
            s_daily.water_ml < s_settings.daily_goal_ml &&
            s_completed_water_cups < s_scheduled_water_cups;
        if (s_water_due) {
            next_seconds = 0;
        }
    }
    s_next_water_seconds = next_seconds;

    if (s_current_sitting_seconds >= 45u * 60u) {
        reminder_level = 3u;
    } else if (s_current_sitting_seconds >= 20u * 60u) {
        reminder_level = 2u;
    } else if (s_current_sitting_seconds >= 10u * 60u) {
        reminder_level = 1u;
    }

    s_care_level = reminder_level;
    if (s_water_due && s_care_level < 2u) {
        s_care_level = 2u;
    }
    snprintf(s_care_action, sizeof(s_care_action), "%s",
             care_action_for_state(reminder_level, s_overtime_active,
                                   s_within_work, s_water_due,
                                   s_time_valid));
}

esp_err_t care_guidance_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    if (!config_service_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    load_persisted_state();
    water_reminder_init(&s_water_reminder, CARE_WATER_NAG_INTERVAL_S);
    s_last_tick_ms = 0;
    s_next_persist_attempt_ms = 0;
    s_daily_dirty = false;
    reset_persist_backoff();
    s_care_level = 0;
    snprintf(s_care_action, sizeof(s_care_action), "%s", "sync_time");
    s_initialized = true;
    ESP_LOGI(TAG,
             "care settings loaded: water=%s, goal=%u ml, cup=%u ml, "
             "work=%02u:%02u-%02u:%02u/%02u:%02u-%02u:%02u, overtime=%s, "
             "alert_mode=%u",
             s_settings.water_enabled ? "on" : "off",
             (unsigned)s_settings.daily_goal_ml,
             (unsigned)s_settings.cup_ml,
             (unsigned)(s_settings.work_start_min / 60u),
             (unsigned)(s_settings.work_start_min % 60u),
             (unsigned)(s_settings.morning_end_min / 60u),
             (unsigned)(s_settings.morning_end_min % 60u),
             (unsigned)(s_settings.afternoon_start_min / 60u),
             (unsigned)(s_settings.afternoon_start_min % 60u),
             (unsigned)(s_settings.work_end_min / 60u),
             (unsigned)(s_settings.work_end_min % 60u),
             s_settings.overtime_enabled ? "on" : "off",
             (unsigned)s_settings.alert_mode);
    return ESP_OK;
}

void care_guidance_tick(uint32_t uptime_ms, int64_t unix_time,
                        bool time_valid, bool sitting, bool present,
                        uint32_t current_sitting_seconds,
                        const sedentary_update_result_t *sedentary_result,
                        care_event_t *care_event)
{
    bool daily_changed = false;
    int32_t date_key = 0;

    if (care_event != NULL) {
        *care_event = CARE_EVENT_NONE;
    }
    if (!s_initialized ||
        xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) != pdTRUE) {
        return;
    }

    if (s_last_tick_ms == 0u) {
        s_last_tick_ms = uptime_ms;
    } else {
        uint32_t elapsed_ms = uptime_ms - s_last_tick_ms;

        s_last_tick_ms = uptime_ms;
        if (elapsed_ms > 5000u) {
            elapsed_ms = 5000u;
        }
        if (sitting && present) {
            s_sitting_fraction_ms += elapsed_ms;
            if (s_sitting_fraction_ms >= 1000u) {
                uint32_t seconds = s_sitting_fraction_ms / 1000u;

                s_daily.today_sitting_seconds += seconds;
                s_sitting_fraction_ms %= 1000u;
                daily_changed = true;
            }
        }
    }

    if (time_valid && unix_time > 0) {
        date_key = local_date_key(unix_time);
        if (date_key != 0 && s_daily.date_key != 0 &&
            date_key != s_daily.date_key) {
            reset_daily(date_key);
            daily_changed = true;
        } else if (s_daily.date_key == 0 && date_key != 0) {
            s_daily.date_key = date_key;
            daily_changed = true;
        }
    }

    s_current_sitting_seconds = current_sitting_seconds;
    if (current_sitting_seconds > s_daily.longest_sitting_seconds) {
        s_daily.longest_sitting_seconds = current_sitting_seconds;
        daily_changed = true;
    }

    if (sedentary_result != NULL) {
        for (uint8_t index = 0u;
             index < sedentary_result->event_count; index++) {
            switch (sedentary_result->events[index].type) {
            case SEDENTARY_EVENT_GENTLE_NUDGE:
                s_daily.gentle_nudges++;
                daily_changed = true;
                break;
            case SEDENTARY_EVENT_ACTIVITY_NUDGE:
                s_daily.activity_nudges++;
                daily_changed = true;
                break;
            case SEDENTARY_EVENT_BREAK_COMPLETED:
                s_daily.completed_breaks++;
                daily_changed = true;
                break;
            default:
                break;
            }
        }
    }

    refresh_derived_state(unix_time, time_valid);
    if (water_reminder_update(&s_water_reminder, unix_time, time_valid,
                              s_water_due) &&
        care_event != NULL) {
        *care_event = CARE_EVENT_WATER_REMINDER;
    }

    if (daily_changed) {
        s_daily_dirty = true;
    }
    if (s_daily_dirty &&
        (s_next_persist_attempt_ms == 0u ||
         uptime_reached(uptime_ms, s_next_persist_attempt_ms))) {
        if (save_daily() == ESP_OK) {
            s_daily_dirty = false;
            s_next_persist_attempt_ms =
                uptime_ms + CARE_PERSIST_INTERVAL_MS;
            reset_persist_backoff();
        } else {
            s_next_persist_attempt_ms = uptime_ms + s_persist_retry_ms;
            if (s_persist_retry_ms < CARE_PERSIST_RETRY_MAX_MS) {
                uint32_t next_retry = s_persist_retry_ms * 2u;

                s_persist_retry_ms =
                    next_retry > CARE_PERSIST_RETRY_MAX_MS
                        ? CARE_PERSIST_RETRY_MAX_MS
                        : next_retry;
            }
        }
    }
    xSemaphoreGive(s_lock);
}

esp_err_t care_guidance_set_settings(const care_settings_t *settings)
{
    esp_err_t err;

    if (!s_initialized || settings == NULL || !valid_settings(settings)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    err = save_settings_record(settings);
    if (err == ESP_OK) {
        s_settings = *settings;
        water_reminder_reset(&s_water_reminder);
        refresh_derived_state(time(NULL), time(NULL) > 0);
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t care_guidance_add_water(int32_t delta_ml)
{
    esp_err_t err;
    care_daily_blob_t next_daily;
    int32_t next_water_ml;

    if (!s_initialized || delta_ml == 0 || delta_ml < -1000 ||
        delta_ml > 1000) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    next_daily = s_daily;
    next_water_ml = (int32_t)next_daily.water_ml + delta_ml;
    if (next_water_ml < 0) {
        next_water_ml = 0;
    }
    if (next_water_ml > (int32_t)CARE_WATER_MAX_TOTAL_ML) {
        next_water_ml = (int32_t)CARE_WATER_MAX_TOTAL_ML;
    }
    next_daily.water_ml = (uint32_t)next_water_ml;
    err = config_service_save_blob(CONFIG_SERVICE_ITEM_CARE_DAILY,
                                   &next_daily, sizeof(next_daily));
    if (err == ESP_OK) {
        s_daily = next_daily;
        s_daily_dirty = false;
        s_next_persist_attempt_ms =
            (uint32_t)(esp_timer_get_time() / 1000) +
            CARE_PERSIST_INTERVAL_MS;
        reset_persist_backoff();
        water_reminder_reset(&s_water_reminder);
        refresh_derived_state(time(NULL), time(NULL) > 0);
    }
    xSemaphoreGive(s_lock);
    return err;
}

void care_guidance_get_snapshot(care_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    if (!s_initialized ||
        xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    snapshot->settings = s_settings;
    snapshot->time_valid = s_time_valid;
    snapshot->within_work = s_within_work;
    snapshot->overtime_active = s_overtime_active;
    snapshot->water_due = s_water_due;
    snapshot->today_sitting_seconds = s_daily.today_sitting_seconds;
    snapshot->longest_sitting_seconds = s_daily.longest_sitting_seconds;
    snapshot->current_sitting_seconds = s_current_sitting_seconds;
    snapshot->completed_breaks = s_daily.completed_breaks;
    snapshot->gentle_nudges = s_daily.gentle_nudges;
    snapshot->activity_nudges = s_daily.activity_nudges;
    snapshot->water_ml = s_daily.water_ml;
    snapshot->water_goal_ml = s_settings.daily_goal_ml;
    snapshot->water_remaining_ml =
        s_daily.water_ml >= s_settings.daily_goal_ml
            ? 0u
            : s_settings.daily_goal_ml - s_daily.water_ml;
    snapshot->next_water_seconds = s_next_water_seconds;
    snapshot->scheduled_water_cups = s_scheduled_water_cups;
    snapshot->completed_water_cups = s_completed_water_cups;
    snapshot->care_level = s_care_level;
    memcpy(snapshot->care_action, s_care_action,
           sizeof(snapshot->care_action));
    snapshot->care_action[sizeof(snapshot->care_action) - 1u] = '\0';
    xSemaphoreGive(s_lock);
}
