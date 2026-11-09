#include "wellness_monitor.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "app_events.h"
#include "app_config.h"
#include "care_coordinator.h"
#include "care_guidance_internal.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "feedback_service.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pir_input.h"
#include "pir_service.h"
#include "rgb_indicator.h"
#include "sedentary_reminder.h"
#include "sht30_uart.h"
#include "ssd1306.h"
#include "time_service.h"
#include "weather_service.h"

#define WELLNESS_TASK_STACK 4096u
#define WELLNESS_TASK_PRIORITY 4u
#define WELLNESS_POLL_MS 20u
#define WELLNESS_OLED_REFRESH_MS 250u
#define WELLNESS_OLED_RETRY_MS 2000u
#define WELLNESS_PIR_PRESENCE_HOLD_MS 120000u
#define WELLNESS_PIR_DEBOUNCE_SAMPLES 3u
#define WELLNESS_SHT30_STALE_MS 5000u
#define WELLNESS_WEATHER_OLED_SLOT_US 8000000LL

static const char *TAG = "wellness";

static app_wellness_config_t s_config;
static pir_input_t s_pir_input;
static pir_service_t s_pir;
static sedentary_state_t s_sedentary;
static TaskHandle_t s_task;
static portMUX_TYPE s_snapshot_mux = portMUX_INITIALIZER_UNLOCKED;
static wellness_snapshot_t s_snapshot;
static bool s_started;
static bool s_uart_ready;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool sample_pir(void *context)
{
    return pir_input_read((const pir_input_t *)context);
}

static void publish_snapshot(const wellness_snapshot_t *snapshot)
{
    taskENTER_CRITICAL(&s_snapshot_mux);
    s_snapshot = *snapshot;
    taskEXIT_CRITICAL(&s_snapshot_mux);
}

static void format_duration(uint32_t duration_ms, char *out, size_t capacity)
{
    uint32_t total_seconds = duration_ms / 1000u;
    uint32_t hours = total_seconds / 3600u;

    if (hours > 99u) {
        hours = 99u;
    }
    (void)snprintf(out, capacity, "%02lu:%02lu:%02lu",
                   (unsigned long)hours,
                   (unsigned long)((total_seconds / 60u) % 60u),
                   (unsigned long)(total_seconds % 60u));
}

static void format_tenths(char *out, size_t capacity, int16_t tenths,
                          const char *unit)
{
    unsigned int magnitude;
    const char *sign = "";

    if (tenths < 0) {
        sign = "-";
        magnitude = (unsigned int)(-tenths);
    } else {
        magnitude = (unsigned int)tenths;
    }
    (void)snprintf(out, capacity, "%s%u.%u%s", sign, magnitude / 10u,
                   magnitude % 10u, unit);
}

static void draw_centered(int y, const char *text, bool color)
{
    int width = (int)strlen(text) * SSD1306_FONT_ADVANCE - 1;
    int x = (SSD1306_WIDTH - width) / 2;

    if (x < 0) {
        x = 0;
    }
    ssd1306_draw_text(x, y, text, color);
}

static void draw_centered_scaled(int y, const char *text, uint8_t scale,
                                 bool color)
{
    int width = ((int)strlen(text) * SSD1306_FONT_ADVANCE - 1) * scale;
    int x = (SSD1306_WIDTH - width) / 2;

    if (x < 0) {
        x = 0;
    }
    ssd1306_draw_text_scaled(x, y, text, scale, color);
}

static void draw_badge(int y, const char *text)
{
    int width = (int)strlen(text) * SSD1306_FONT_ADVANCE + 8;
    int x = (SSD1306_WIDTH - width) / 2;

    ssd1306_fill_rect(x, y, width, 11, true);
    ssd1306_draw_text(x + 4, y + 2, text, false);
}

static const char *oled_care_label(const care_snapshot_t *care)
{
    if (care == NULL) {
        return "CARE WAITING";
    }
    if (strcmp(care->care_action, "stand_up_now") == 0) {
        return "STAND UP NOW";
    }
    if (strcmp(care->care_action, "overtime_stand_now") == 0) {
        return "STAND + WATER";
    }
    if (strcmp(care->care_action, "walk_or_restroom") == 0) {
        return "WALK 2-3 MIN";
    }
    if (strcmp(care->care_action, "overtime_walk_water") == 0) {
        return "WALK + SIP";
    }
    if (strcmp(care->care_action, "drink_water") == 0) {
        return "DRINK WATER";
    }
    if (strcmp(care->care_action, "neck_break") == 0) {
        return "NECK + EYES 20S";
    }
    if (strcmp(care->care_action, "sync_time") == 0) {
        return "OPEN WEB TO SYNC";
    }
    if (strcmp(care->care_action, "rest_time") == 0) {
        return "REST TIME";
    }
    if (strcmp(care->care_action, "overtime_focus") == 0) {
        return "OVERTIME MODE";
    }
    return "KEEP MOVING";
}

static void render_oled(const wellness_snapshot_t *snapshot,
                        const care_snapshot_t *care)
{
    char temperature[12];
    char humidity[12];
    char line[40];
    char duration[16];
    const char *badge;

    ssd1306_clear();
    ssd1306_draw_text(0, 0, "WELLNESS", true);
    (void)snprintf(line, sizeof(line), "PIR %u",
                   snapshot->pir_motion ? 1u : 0u);
    ssd1306_draw_text(SSD1306_WIDTH - (int)strlen(line) * 6, 0, line, true);

    if (snapshot->sht30_valid) {
        format_tenths(temperature, sizeof(temperature),
                      snapshot->temperature_tenths_c, "C");
        format_tenths(humidity, sizeof(humidity),
                      (int16_t)snapshot->humidity_tenths_rh, "%");
        (void)snprintf(line, sizeof(line), "T %s H %s",
                       temperature, humidity);
    } else {
        (void)snprintf(line, sizeof(line), "T --.-C H --.-%%");
    }
    draw_centered(10, line, true);

    if (snapshot->sedentary_active) {
        format_duration(snapshot->sitting_elapsed_ms, duration,
                        sizeof(duration));
        draw_centered_scaled(23, duration, 2u, true);
    } else {
        draw_centered_scaled(23, "--:--:--", 2u, true);
    }

    if (!snapshot->sedentary_active) {
        badge = "NO SESSION";
    } else if (!snapshot->sedentary_present) {
        badge = "ON BREAK";
    } else if (snapshot->sedentary_alert_active) {
        badge = "STAND UP";
    } else {
        badge = "SITTING";
    }
    draw_badge(44, badge);

    if (care != NULL && care->water_due) {
        draw_centered(57, "DRINK WATER NOW", true);
    } else if (!snapshot->sedentary_active) {
        draw_centered(57, "MOVE TO START", true);
    } else if (!snapshot->sedentary_present) {
        format_duration(snapshot->break_remaining_ms, duration,
                        sizeof(duration));
        (void)snprintf(line, sizeof(line), "VALID IN %s", duration);
        draw_centered(57, line, true);
    } else {
        format_duration(snapshot->next_reminder_ms, duration,
                        sizeof(duration));
        (void)snprintf(line, sizeof(line), "%s", oled_care_label(care));
        draw_centered(57, line, true);
    }
}

static const char *oled_weather_condition(int code)
{
    if (code == 0) {
        return "CLEAR";
    }
    if (code <= 2) {
        return "PARTLY CLOUDY";
    }
    if (code == 3) {
        return "OVERCAST";
    }
    if (code == 45 || code == 48) {
        return "FOG";
    }
    if (code >= 51 && code <= 57) {
        return "DRIZZLE";
    }
    if (code >= 61 && code <= 67) {
        return "RAIN";
    }
    if (code >= 71 && code <= 77) {
        return "SNOW";
    }
    if (code >= 80 && code <= 82) {
        return "SHOWERS";
    }
    if (code >= 85 && code <= 86) {
        return "SNOW SHOWERS";
    }
    if (code >= 95) {
        return "THUNDER";
    }
    return "WEATHER";
}

static void render_weather_oled(
    const weather_service_snapshot_t *weather)
{
    char line[32];
    const char *observed = weather->observed_at;

    ssd1306_clear();
    draw_centered(0, weather->stale ? "WEATHER CACHE" : "OUTDOOR NOW",
                  true);
    (void)snprintf(line, sizeof(line), "%.1fC", weather->temperature_c);
    draw_centered_scaled(13, line, 2u, true);
    draw_centered(33, oled_weather_condition(weather->weather_code), true);
    (void)snprintf(line, sizeof(line), "H %d%%  L/H %d/%dC",
                   weather->humidity_percent,
                   (int)lround(weather->temperature_min_c),
                   (int)lround(weather->temperature_max_c));
    draw_centered(45, line, true);
    if (strlen(observed) >= 16 && observed[10] == 'T') {
        observed += 11;
    }
    (void)snprintf(line, sizeof(line), "%s %.*s",
                   weather->stale ? "CACHE" : "UPDATED", 5, observed);
    draw_centered(56, line, true);
}

static void refresh_snapshot(wellness_snapshot_t *snapshot,
                             const pir_service_snapshot_t *pir,
                             const sedentary_snapshot_t *sedentary)
{
    feedback_service_snapshot_t feedback = {0};
    sht30_reading_t reading = {0};
    sht30_stats_t stats = sht30_uart_get_stats();
    uint32_t age_ms = 0u;

    snapshot->pir_present = pir->present;
    snapshot->pir_motion = pir->motion;
    snapshot->pir_motion_events = pir->motion_events;

    snapshot->sht30_valid = sht30_uart_get_latest(&reading, &age_ms);
    snapshot->sht30_online =
        snapshot->sht30_valid && age_ms <= WELLNESS_SHT30_STALE_MS;
    snapshot->temperature_tenths_c = reading.temperature_tenths_c;
    snapshot->humidity_tenths_rh = reading.humidity_tenths_rh;
    snapshot->sht30_age_ms = age_ms;
    snapshot->sht30_frames = stats.frames;
    snapshot->sht30_bad_frames = stats.bad;

    snapshot->oled_ready = ssd1306_is_ready();
    snapshot->oled_address = ssd1306_address();

    snapshot->sedentary_active = sedentary->active;
    snapshot->sedentary_present = sedentary->present;
    snapshot->sedentary_alert_active = sedentary->alert_active;
    snapshot->sedentary_phase = sedentary_phase_name(sedentary->phase);
    (void)feedback_service_get_snapshot(&feedback);
    snapshot->beeping = feedback.beeping;
    snapshot->vibrating = feedback.vibrating;
    snapshot->beep_count = feedback.beep_count;
    snapshot->sitting_elapsed_ms = sedentary->sitting_elapsed_ms;
    snapshot->next_reminder_ms = sedentary->next_reminder_ms;
    snapshot->break_remaining_ms = sedentary->break_remaining_ms;
    snapshot->alert_starts = sedentary->alert_starts;
    snapshot->escalations = sedentary->escalations;
    snapshot->gentle_nudges = sedentary->gentle_nudges;
    snapshot->activity_nudges = sedentary->activity_nudges;
    snapshot->short_breaks = sedentary->short_breaks;
    snapshot->completed_breaks = sedentary->completed_breaks;
    snapshot->reminder_level = sedentary->reminder_level;
}

static app_event_type_t app_event_type_for_sedentary(
    sedentary_event_t event)
{
    switch (event) {
    case SEDENTARY_EVENT_GENTLE_NUDGE:
        return APP_EVENT_SEDENTARY_GENTLE_NUDGE;
    case SEDENTARY_EVENT_ACTIVITY_NUDGE:
        return APP_EVENT_SEDENTARY_ACTIVITY_NUDGE;
    case SEDENTARY_EVENT_ALERT_STARTED:
        return APP_EVENT_SEDENTARY_ALERT_STARTED;
    case SEDENTARY_EVENT_ALERT_ESCALATED:
        return APP_EVENT_SEDENTARY_ALERT_ESCALATED;
    case SEDENTARY_EVENT_SHORT_BREAK_RETURN:
        return APP_EVENT_SEDENTARY_SHORT_BREAK_RETURN;
    case SEDENTARY_EVENT_BREAK_COMPLETED:
        return APP_EVENT_SEDENTARY_BREAK_COMPLETED;
    case SEDENTARY_EVENT_NONE:
    default:
        return APP_EVENT_NONE;
    }
}

static int32_t value_for_sedentary(
    const sedentary_event_record_t *event)
{
    return event->reminder_level;
}

static app_event_severity_t severity_for_sedentary(
    const sedentary_event_record_t *event)
{
    if (event->reminder_level >= 3u) {
        return APP_EVENT_SEVERITY_ALERT;
    }
    if (event->reminder_level >= 2u) {
        return APP_EVENT_SEVERITY_WARNING;
    }
    return APP_EVENT_SEVERITY_INFO;
}

static void publish_sedentary_transition(
    const sedentary_transition_t *transition)
{
    esp_err_t err = app_events_publish(
        APP_EVENT_SEDENTARY_PHASE_CHANGED,
        APP_EVENT_SOURCE_WELLNESS, APP_EVENT_SEVERITY_INFO,
        (int32_t)transition->to, (int32_t)transition->from,
        (int32_t)transition->reason);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "phase event publish failed: %s",
                 esp_err_to_name(err));
    }
}

static void publish_sedentary_event(
    const sedentary_event_record_t *event)
{
    app_event_type_t type = app_event_type_for_sedentary(event->type);
    esp_err_t err;

    if (event->type == SEDENTARY_EVENT_NONE || type == APP_EVENT_NONE) {
        return;
    }
    err = app_events_publish(
        type, APP_EVENT_SOURCE_WELLNESS,
        severity_for_sedentary(event), value_for_sedentary(event), 0, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "event publish failed: %s", esp_err_to_name(err));
    }
}

static void publish_sedentary_outputs(
    const sedentary_update_result_t *result)
{
    uint8_t transition_index = 0u;
    uint8_t event_index = 0u;

    while (transition_index < result->transition_count &&
           event_index < result->event_count) {
        const sedentary_transition_t *transition =
            &result->transitions[transition_index];
        const sedentary_event_record_t *event =
            &result->events[event_index];

        if (transition->order < event->order) {
            publish_sedentary_transition(transition);
            transition_index++;
        } else {
            publish_sedentary_event(event);
            event_index++;
        }
    }

    while (transition_index < result->transition_count) {
        publish_sedentary_transition(&result->transitions[transition_index]);
        transition_index++;
    }

    while (event_index < result->event_count) {
        publish_sedentary_event(&result->events[event_index]);
        event_index++;
    }
}

static void wellness_event_handler(const app_event_t *event, void *context)
{
    (void)context;
    if (event == NULL) {
        return;
    }

    switch (event->type) {
    case APP_EVENT_SEDENTARY_GENTLE_NUDGE:
        ESP_LOGI(TAG, "10-minute nudge; relax shoulders and look away");
        break;
    case APP_EVENT_SEDENTARY_ACTIVITY_NUDGE:
        ESP_LOGI(TAG, "20-minute nudge; stand up, walk, or get water");
        break;
    case APP_EVENT_SEDENTARY_ALERT_STARTED:
        ESP_LOGI(TAG, "sedentary alert started; stand up for at least %lu min",
                 (unsigned long)(s_config.sedentary_break_ms / 60000u));
        break;
    case APP_EVENT_SEDENTARY_ALERT_ESCALATED:
        ESP_LOGW(TAG, "still seated; repeated reminder");
        break;
    case APP_EVENT_SEDENTARY_SHORT_BREAK_RETURN:
        ESP_LOGW(TAG, "short break detected; repeated reminder");
        break;
    case APP_EVENT_SEDENTARY_BREAK_COMPLETED:
        ESP_LOGI(TAG, "valid break completed; sedentary timer reset");
        break;
    case APP_EVENT_SEDENTARY_PHASE_CHANGED:
        ESP_LOGI(TAG, "sedentary phase %s -> %s (%s)",
                 sedentary_phase_name((sedentary_phase_t)event->detail),
                 sedentary_phase_name((sedentary_phase_t)event->value),
                 sedentary_transition_reason_name(
                     (sedentary_transition_reason_t)event->cause));
        break;
    case APP_EVENT_CARE_WATER_REMINDER: {
        care_snapshot_t care = {0};

        care_guidance_get_snapshot(&care);
        ESP_LOGI(TAG, "water reminder: %lu/%lu ml, next cup target %u",
                 (unsigned long)care.water_ml,
                 (unsigned long)care.water_goal_ml,
                 (unsigned)care.scheduled_water_cups);
        break;
    }
    default:
        break;
    }
}

static feedback_mode_t feedback_mode_from_care(care_alert_mode_t mode)
{
    switch (mode) {
    case CARE_ALERT_MODE_VIBRATE:
        return FEEDBACK_MODE_VIBRATE;
    case CARE_ALERT_MODE_SOUND:
        return FEEDBACK_MODE_SOUND;
    case CARE_ALERT_MODE_SILENT:
        return FEEDBACK_MODE_SILENT;
    case CARE_ALERT_MODE_BOTH:
    default:
        return FEEDBACK_MODE_BOTH;
    }
}

static feedback_scene_t feedback_scene_from_care(care_scene_t scene)
{
    switch (scene) {
    case CARE_SCENE_WATER_REMINDER:
        return FEEDBACK_SCENE_WATER_REMINDER;
    case CARE_SCENE_SEDENTARY_ALERT:
        return FEEDBACK_SCENE_SEDENTARY_ALERT;
    case CARE_SCENE_SEDENTARY_ESCALATED:
        return FEEDBACK_SCENE_SEDENTARY_ESCALATED;
    case CARE_SCENE_NONE:
    default:
        return FEEDBACK_SCENE_NONE;
    }
}

static void wellness_task(void *argument)
{
    TickType_t last_wake = xTaskGetTickCount();
    int64_t last_oled_us = 0;
    int64_t last_oled_attempt_us = 0;
    bool oled_failure_logged = false;

    (void)argument;

    while (true) {
        uint32_t current_ms = now_ms();
        int64_t current_us = esp_timer_get_time();
        pir_service_update_result_t pir_result;
        sedentary_update_result_t sedentary_result;
        care_event_t care_event = CARE_EVENT_NONE;
        care_coordinator_decision_t care_decision;
        care_snapshot_t care_snapshot = {0};
        time_service_snapshot_t time_snapshot;
        wellness_snapshot_t snapshot = {0};

        if (s_uart_ready) {
            sht30_uart_poll();
        }

        pir_service_update(&s_pir, current_ms, &pir_result);
        for (uint8_t index = 0u; index < pir_result.event_count; index++) {
            switch (pir_result.events[index].type) {
            case PIR_SERVICE_EVENT_MOTION_STARTED:
                ESP_LOGI(TAG, "PIR motion event %lu",
                         (unsigned long)pir_result.snapshot.motion_events);
                break;
            case PIR_SERVICE_EVENT_PRESENCE_EXPIRED:
                ESP_LOGI(TAG, "PIR presence expired after %lu ms",
                         (unsigned long)WELLNESS_PIR_PRESENCE_HOLD_MS);
                break;
            case PIR_SERVICE_EVENT_NONE:
            default:
                break;
            }
        }

        sedentary_update(&s_sedentary, current_ms,
                         pir_result.snapshot.motion,
                         &sedentary_result);
        if (sedentary_result.event_overflow) {
            ESP_LOGW(TAG,
                     "sedentary event output overflow; events were dropped");
        }
        if (sedentary_result.transition_overflow) {
            ESP_LOGW(TAG,
                     "sedentary transition output overflow; transitions were dropped");
        }
        publish_sedentary_outputs(&sedentary_result);
        time_service_get_snapshot(&time_snapshot);
        care_guidance_tick(
            current_ms, time_snapshot.unix_time, time_snapshot.valid,
            sedentary_result.snapshot.active,
            sedentary_result.snapshot.present,
            sedentary_result.snapshot.sitting_elapsed_ms / 1000u,
            &sedentary_result, &care_event);
        care_coordinator_evaluate(
            &sedentary_result,
            care_event == CARE_EVENT_WATER_REMINDER,
            &care_decision);
        if (care_decision.water_event) {
            esp_err_t err = app_events_publish(
                APP_EVENT_CARE_WATER_REMINDER, APP_EVENT_SOURCE_CARE,
                APP_EVENT_SEVERITY_ALERT, 0, 0, 0);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "water event publish failed: %s",
                         esp_err_to_name(err));
            }
        }
        care_guidance_get_snapshot(&care_snapshot);
        rgb_indicator_set_wellness_state(
            sedentary_result.snapshot.active,
            sedentary_result.snapshot.present,
            sedentary_result.snapshot.alert_active,
            care_snapshot.water_due);
        (void)feedback_service_set_mode(
            feedback_mode_from_care(care_snapshot.settings.alert_mode));
        feedback_scene_t feedback_scene =
            feedback_scene_from_care(care_decision.scene);
        if (feedback_scene != FEEDBACK_SCENE_NONE) {
            esp_err_t feedback_err =
                feedback_service_play(feedback_scene);
            if (feedback_err != ESP_OK) {
                ESP_LOGW(TAG, "feedback play failed: %s",
                         esp_err_to_name(feedback_err));
            }
        }

        if (!ssd1306_is_ready() &&
            current_us - last_oled_attempt_us >=
                (WELLNESS_OLED_RETRY_MS * 1000LL)) {
            const ssd1306_config_t oled_config = {
                .i2c_port = s_config.oled_i2c_port,
                .sda_gpio = s_config.oled_sda_gpio,
                .scl_gpio = s_config.oled_scl_gpio,
                .clock_hz = s_config.oled_i2c_hz,
            };
            esp_err_t err = ssd1306_init(&oled_config);

            last_oled_attempt_us = current_us;
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "SSD1306 ready at 0x%02X",
                         ssd1306_address());
                oled_failure_logged = false;
            } else if (!oled_failure_logged) {
                ESP_LOGW(TAG,
                         "SSD1306 not found at 0x3C/0x3D; retrying: %s",
                         esp_err_to_name(err));
                oled_failure_logged = true;
            }
        }

        refresh_snapshot(&snapshot, &pir_result.snapshot,
                         &sedentary_result.snapshot);
        publish_snapshot(&snapshot);

        if (snapshot.oled_ready &&
            current_us - last_oled_us >=
                (WELLNESS_OLED_REFRESH_MS * 1000LL)) {
            weather_service_snapshot_t weather_snapshot = {0};

            weather_service_get_snapshot(&weather_snapshot);
            if (weather_snapshot.available &&
                (current_us / WELLNESS_WEATHER_OLED_SLOT_US) % 2LL != 0) {
                render_weather_oled(&weather_snapshot);
            } else {
                render_oled(&snapshot, &care_snapshot);
            }
            if (ssd1306_flush() != ESP_OK && !oled_failure_logged) {
                ESP_LOGW(TAG, "SSD1306 write failed; retrying");
                oled_failure_logged = true;
                snapshot.oled_ready = false;
                snapshot.oled_address = 0u;
                publish_snapshot(&snapshot);
            }
            last_oled_us = current_us;
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(WELLNESS_POLL_MS));
    }
}

esp_err_t wellness_monitor_start(void)
{
    const app_config_t *app = app_config_get();
    const sedentary_config_t sedentary_config = {
        .gentle_nudge_ms = 10u * 60u * 1000u,
        .activity_nudge_ms = 20u * 60u * 1000u,
        .alert_after_ms = app->wellness.sedentary_initial_ms,
        .escalation_ms = app->wellness.sedentary_escalation_ms,
        .break_ms = app->wellness.sedentary_break_ms,
        .absence_ms = app->wellness.sedentary_absence_ms,
    };
    esp_err_t err;

    if (s_started) {
        return ESP_OK;
    }
    if (!app->wellness.enabled) {
        return ESP_ERR_INVALID_STATE;
    }

    s_config = app->wellness;
    sedentary_init(&s_sedentary, now_ms(), &sedentary_config);
    err = care_guidance_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "care guidance init failed: %s",
                 esp_err_to_name(err));
        return err;
    }
    err = app_events_init();
    if (err == ESP_OK) {
        err = app_events_subscribe(wellness_event_handler, NULL);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "event subscription failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    err = pir_input_init(&s_pir_input, s_config.pir_gpio);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PIR GPIO %d init failed: %s", s_config.pir_gpio,
                 esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "PIR input ready on GPIO%d", s_config.pir_gpio);
    }
    const pir_service_config_t pir_config = {
        .presence_hold_ms = WELLNESS_PIR_PRESENCE_HOLD_MS,
        .debounce_samples = WELLNESS_PIR_DEBOUNCE_SAMPLES,
    };
    pir_service_init(&s_pir, &pir_config, sample_pir, &s_pir_input);

    err = sht30_uart_init(s_config.sht30_uart_port,
                          s_config.sht30_tx_gpio,
                          s_config.sht30_rx_gpio);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SHT30 UART%d init failed: %s", s_config.sht30_uart_port,
                 esp_err_to_name(err));
    } else {
        s_uart_ready = true;
        ESP_LOGI(TAG, "SHT30 UART%d ready at 9600-8N1 (TX=%d RX=%d)",
                 s_config.sht30_uart_port, s_config.sht30_tx_gpio,
                 s_config.sht30_rx_gpio);
    }

    if (s_config.rgb_led_enabled) {
        const rgb_indicator_config_t rgb_config = {
            .enabled = true,
            .red_gpio = s_config.rgb_red_gpio,
            .green_gpio = s_config.rgb_green_gpio,
            .blue_gpio = s_config.rgb_blue_gpio,
        };

        err = rgb_indicator_init(&rgb_config);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "RGB indicator init failed: %s",
                     esp_err_to_name(err));
        }
    } else {
        ESP_LOGI(TAG, "RGB indicator disabled");
    }

    if (xTaskCreate(wellness_task, "wellness", WELLNESS_TASK_STACK, NULL,
                    WELLNESS_TASK_PRIORITY, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    ESP_LOGI(TAG, "monitor started; initial=%lu min, escalation=%lu min, "
                  "break=%lu min",
             (unsigned long)(s_config.sedentary_initial_ms / 60000u),
             (unsigned long)(s_config.sedentary_escalation_ms / 60000u),
             (unsigned long)(s_config.sedentary_break_ms / 60000u));
    return ESP_OK;
}

bool wellness_monitor_get_snapshot(wellness_snapshot_t *snapshot)
{
    if (snapshot == NULL || !s_started) {
        return false;
    }

    taskENTER_CRITICAL(&s_snapshot_mux);
    *snapshot = s_snapshot;
    taskEXIT_CRITICAL(&s_snapshot_mux);
    return true;
}
