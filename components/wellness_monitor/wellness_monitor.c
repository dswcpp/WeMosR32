#include "wellness_monitor.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "app_events.h"
#include "app_config.h"
#include "button_input.h"
#include "button_service.h"
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
#define WELLNESS_TEMPORARY_MUTE_MS (30u * 60u * 1000u)

static const char *TAG = "wellness";

typedef enum {
    OLED_PAGE_AUTO = 0,
    OLED_PAGE_CARE,
    OLED_PAGE_WATER,
    OLED_PAGE_ENVIRONMENT,
    OLED_PAGE_WEATHER,
} oled_page_mode_t;

static app_wellness_config_t s_config;
static pir_input_t s_pir_input;
static pir_service_t s_pir;
static button_input_t s_button_input;
static button_service_t s_buttons;
static sedentary_state_t s_sedentary;
static TaskHandle_t s_task;
static portMUX_TYPE s_snapshot_mux = portMUX_INITIALIZER_UNLOCKED;
static wellness_snapshot_t s_snapshot;
static bool s_started;
static bool s_uart_ready;
static bool s_buttons_ready;
static oled_page_mode_t s_oled_page = OLED_PAGE_AUTO;
static uint32_t s_temporary_mute_until_ms;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool uptime_reached(uint32_t current_ms, uint32_t deadline_ms)
{
    return (int32_t)(current_ms - deadline_ms) >= 0;
}

static bool temporary_mute_active(uint32_t current_ms)
{
    if (s_temporary_mute_until_ms == 0u) {
        return false;
    }
    if (uptime_reached(current_ms, s_temporary_mute_until_ms)) {
        s_temporary_mute_until_ms = 0u;
        ESP_LOGI(TAG, "30-minute temporary mute expired");
        return false;
    }
    return true;
}

static const char *care_alert_mode_name(care_alert_mode_t mode)
{
    switch (mode) {
    case CARE_ALERT_MODE_VIBRATE:
        return "vibrate";
    case CARE_ALERT_MODE_SOUND:
        return "sound";
    case CARE_ALERT_MODE_SILENT:
        return "silent";
    case CARE_ALERT_MODE_BOTH:
    default:
        return "both";
    }
}

static const char *oled_page_name(oled_page_mode_t page)
{
    switch (page) {
    case OLED_PAGE_CARE:
        return "care";
    case OLED_PAGE_WATER:
        return "water";
    case OLED_PAGE_ENVIRONMENT:
        return "environment";
    case OLED_PAGE_WEATHER:
        return "weather";
    case OLED_PAGE_AUTO:
    default:
        return "auto";
    }
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

static void draw_text_right(int y, const char *text, bool color)
{
    int width = (int)strlen(text) * SSD1306_FONT_ADVANCE - 1;
    int x = SSD1306_WIDTH - width - 4;

    if (x < 0) {
        x = 0;
    }
    ssd1306_draw_text(x, y, text, color);
}

static void draw_header(const char *title, const char *status,
                        bool status_active)
{
    int status_width = (int)strlen(status) * SSD1306_FONT_ADVANCE - 1;
    int status_x = SSD1306_WIDTH - status_width - 4;

    ssd1306_fill_rect(0, 0, SSD1306_WIDTH, 10, true);
    ssd1306_draw_text(3, 1, title, false);
    if (status_active) {
        ssd1306_fill_rect(status_x - 3, 1, status_width + 6, 8, false);
        ssd1306_draw_text(status_x, 2, status, true);
    } else {
        ssd1306_draw_text(status_x, 1, status, false);
    }
}

static void draw_rule(int y)
{
    ssd1306_fill_rect(4, y, SSD1306_WIDTH - 8, 1, true);
}

static void draw_progress_bar(int x, int y, int width, int height,
                              uint32_t percent)
{
    const int inner_x = x + 2;
    const int inner_y = y + 2;
    const int inner_width = width - 4;
    const int inner_height = height - 4;

    if (width < 4 || height < 4) {
        return;
    }
    if (percent > 100u) {
        percent = 100u;
    }

    ssd1306_fill_rect(x, y, width, 1, true);
    ssd1306_fill_rect(x, y + height - 1, width, 1, true);
    ssd1306_fill_rect(x, y, 1, height, true);
    ssd1306_fill_rect(x + width - 1, y, 1, height, true);
    if (percent > 0u) {
        int fill_width =
            (inner_width * (int)percent + 99) / 100;

        if (fill_width <= 0) {
            fill_width = 1;
        }
        ssd1306_fill_rect(inner_x, inner_y, fill_width, inner_height, true);
    }
}

typedef struct {
    const char *text;
    bool active;
} oled_footer_hint_t;

static void draw_button_row(const wellness_snapshot_t *snapshot,
                            const care_snapshot_t *care, bool temporary_mute,
                            oled_page_mode_t page)
{
    const bool silent_mode =
        care != NULL && care->settings.alert_mode == CARE_ALERT_MODE_SILENT;
    oled_footer_hint_t hints[3];
    size_t hint_count;
    int total_width = 0;
    int x;

    if (snapshot->feedback_active) {
        hints[0] = (oled_footer_hint_t){"1 STOP", true};
        if (page == OLED_PAGE_WATER) {
            hints[1] = (oled_footer_hint_t){"2 CUP", false};
        } else if (page == OLED_PAGE_CARE || page == OLED_PAGE_AUTO) {
            hints[1] = (oled_footer_hint_t){
                "2 H2O", care != NULL && care->water_due};
        } else {
            hints[1] = (oled_footer_hint_t){
                "3 MUTE", temporary_mute || silent_mode};
        }
        hints[2] = (oled_footer_hint_t){
            "4 PAGE", snapshot->oled_manual_page};
        hint_count = 3u;
    } else if (page == OLED_PAGE_WATER) {
        hints[0] = (oled_footer_hint_t){"2 CUP", false};
        hints[1] = (oled_footer_hint_t){
            "3 MUTE", temporary_mute || silent_mode};
        hints[2] = (oled_footer_hint_t){
            "4 PAGE", snapshot->oled_manual_page};
        hint_count = 3u;
    } else if (page == OLED_PAGE_CARE || page == OLED_PAGE_AUTO) {
        hints[0] = (oled_footer_hint_t){
            "2 H2O", care != NULL && care->water_due};
        hints[1] = (oled_footer_hint_t){
            "3 MUTE", temporary_mute || silent_mode};
        hints[2] = (oled_footer_hint_t){
            "4 PAGE", snapshot->oled_manual_page};
        hint_count = 3u;
    } else {
        hints[0] = (oled_footer_hint_t){"2 CUP", false};
        hints[1] = (oled_footer_hint_t){
            "3 MUTE", temporary_mute || silent_mode};
        hints[2] = (oled_footer_hint_t){
            "4 PAGE", snapshot->oled_manual_page};
        hint_count = 3u;
    }

    for (size_t index = 0u; index < hint_count; index++) {
        total_width +=
            (int)strlen(hints[index].text) * SSD1306_FONT_ADVANCE - 1;
    }
    total_width += 7 * ((int)hint_count - 1);
    x = (SSD1306_WIDTH - total_width) / 2;
    if (x < 0) {
        x = 0;
    }

    for (size_t index = 0u; index < hint_count; index++) {
        int width =
            (int)strlen(hints[index].text) * SSD1306_FONT_ADVANCE - 1;

        if (hints[index].active) {
            ssd1306_fill_rect(x - 2, 56, width + 4, 8, true);
            ssd1306_draw_text(x, 57, hints[index].text, false);
        } else {
            ssd1306_draw_text(x, 57, hints[index].text, true);
        }
        x += width + 7;
    }
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
        return "STAND AND WATER";
    }
    if (strcmp(care->care_action, "walk_or_restroom") == 0) {
        return "WALK 2-3 MIN";
    }
    if (strcmp(care->care_action, "overtime_walk_water") == 0) {
        return "WALK AND SIP";
    }
    if (strcmp(care->care_action, "drink_water") == 0) {
        return "DRINK WATER";
    }
    if (strcmp(care->care_action, "neck_break") == 0) {
        return "NECK AND EYE 20S";
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

static void render_care_oled(const wellness_snapshot_t *snapshot,
                             const care_snapshot_t *care,
                             bool temporary_mute)
{
    char duration[16];
    const bool silent_mode =
        care != NULL && care->settings.alert_mode == CARE_ALERT_MODE_SILENT;
    const bool water_due = care != NULL && care->water_due;
    const char *status;
    const char *descriptor;
    const char *action;

    if (snapshot->sedentary_active) {
        format_duration(snapshot->sitting_elapsed_ms, duration,
                        sizeof(duration));
    } else {
        (void)snprintf(duration, sizeof(duration), "--:--:--");
    }

    if (temporary_mute || silent_mode) {
        status = "MUTED";
    } else if (water_due) {
        status = "WATER";
    } else if (!snapshot->sedentary_active) {
        status = "IDLE";
    } else if (!snapshot->sedentary_present) {
        status = "BREAK";
    } else if (snapshot->sedentary_alert_active) {
        status = "ALERT";
    } else {
        status = "SIT";
    }

    ssd1306_clear();
    draw_header("CARE", status,
                temporary_mute || silent_mode || water_due ||
                    snapshot->sedentary_alert_active);
    draw_centered_scaled(12, duration, 2u, true);
    draw_rule(29);

    if (!snapshot->sedentary_active) {
        descriptor = "NO ACTIVE SESSION";
        action = "MOVE TO START";
    } else if (!snapshot->sedentary_present) {
        format_duration(snapshot->break_remaining_ms, duration,
                        sizeof(duration));
        descriptor = "BREAK IN PROGRESS";
        action = duration;
    } else if (water_due) {
        descriptor = "SITTING TIME";
        action = "DRINK WATER NOW";
    } else if (snapshot->sedentary_alert_active) {
        descriptor = "TOO LONG SITTING";
        action = oled_care_label(care);
    } else {
        descriptor = "SITTING TIME";
        action = oled_care_label(care);
    }

    draw_centered(33, descriptor, true);
    draw_centered(45, action, true);
}

static void render_water_oled(const care_snapshot_t *care)
{
    char line[40];
    uint32_t percent = 0u;

    ssd1306_clear();
    draw_header("WATER", care != NULL && care->water_due ? "DUE" : "TODAY",
                care != NULL && care->water_due);
    if (care == NULL || care->settings.cup_ml == 0u) {
        draw_centered_scaled(12, "-- ML", 2u, true);
        draw_centered(34, "CARE DATA WAITING", true);
        return;
    }

    if (care->water_goal_ml > 0u) {
        uint32_t displayed = care->water_ml;

        if (displayed > care->water_goal_ml) {
            displayed = care->water_goal_ml;
        }
        percent = (displayed * 100u) / care->water_goal_ml;
    }
    (void)snprintf(line, sizeof(line), "%lu ML",
                   (unsigned long)care->water_ml);
    draw_centered_scaled(12, line, 2u, true);
    draw_progress_bar(8, 29, SSD1306_WIDTH - 16, 6, percent);
    (void)snprintf(line, sizeof(line), "%lu%%  GOAL %lu ML",
                   (unsigned long)percent,
                   (unsigned long)care->water_goal_ml);
    draw_centered(38, line, true);
    if (!care->time_valid) {
        (void)snprintf(line, sizeof(line), "TIME SYNC NEEDED");
    } else if (care->water_remaining_ml == 0u) {
        (void)snprintf(line, sizeof(line), "GOAL REACHED");
    } else {
        uint32_t remaining_cups =
            (care->water_remaining_ml + care->settings.cup_ml - 1u) /
            care->settings.cup_ml;

        (void)snprintf(line, sizeof(line), "%lu CUPS LEFT",
                       (unsigned long)remaining_cups);
    }
    draw_centered(48, line, true);
}

static const char *oled_comfort_label(int16_t temperature_tenths_c,
                                      uint16_t humidity_tenths_rh)
{
    if (temperature_tenths_c < 200) {
        return "TOO COOL";
    }
    if (temperature_tenths_c > 270) {
        return "TOO WARM";
    }
    if (humidity_tenths_rh < 300u) {
        return "DRY AIR";
    }
    if (humidity_tenths_rh > 650u) {
        return "HUMID AIR";
    }
    return "COMFORTABLE";
}

static void render_environment_oled(const wellness_snapshot_t *snapshot)
{
    char temperature[12];
    char humidity[12];
    char line[40];

    ssd1306_clear();
    draw_header("ROOM", snapshot->pir_present ? "HERE" : "AWAY", false);
    if (snapshot->sht30_valid) {
        format_tenths(temperature, sizeof(temperature),
                      snapshot->temperature_tenths_c, "C");
        format_tenths(humidity, sizeof(humidity),
                      (int16_t)snapshot->humidity_tenths_rh, "%");
        draw_centered_scaled(12, temperature, 2u, true);
        draw_rule(29);
        (void)snprintf(line, sizeof(line), "HUMIDITY %s", humidity);
        draw_centered(33, line, true);
        draw_centered(
            45,
            oled_comfort_label(snapshot->temperature_tenths_c,
                               snapshot->humidity_tenths_rh),
            true);
    } else {
        draw_centered_scaled(12, "--.-C", 2u, true);
        draw_centered(33, "SENSOR WAITING", true);
        draw_centered(45, "NO ROOM DATA", true);
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
    const char *status = weather->available
                             ? weather->stale ? "CACHE" : "LIVE"
                             : "WAIT";
    const char *condition;

    ssd1306_clear();
    draw_header("WEATHER", status, weather->stale);
    if (!weather->available) {
        draw_centered_scaled(12, "--.-C", 2u, true);
        draw_centered(33, "NO WEATHER DATA", true);
        draw_centered(45, "CHECK NETWORK", true);
        return;
    }

    (void)snprintf(line, sizeof(line), "%.1fC", weather->temperature_c);
    draw_centered_scaled(12, line, 2u, true);
    draw_rule(29);
    condition = oled_weather_condition(weather->weather_code);
    draw_centered(33, condition, true);
    (void)snprintf(line, sizeof(line), "HUM %d%%",
                   weather->humidity_percent);
    ssd1306_draw_text(4, 45, line, true);
    (void)snprintf(line, sizeof(line), "H/L %d/%dC",
                   (int)lround(weather->temperature_min_c),
                   (int)lround(weather->temperature_max_c));
    draw_text_right(45, line, true);
}

static oled_page_mode_t selected_oled_page(bool weather_available,
                                           int64_t current_us)
{
    if (s_oled_page != OLED_PAGE_AUTO) {
        return s_oled_page;
    }
    if (weather_available &&
        (current_us / WELLNESS_WEATHER_OLED_SLOT_US) % 2LL != 0) {
        return OLED_PAGE_WEATHER;
    }
    return OLED_PAGE_CARE;
}

static void render_oled(const wellness_snapshot_t *snapshot,
                        const care_snapshot_t *care,
                        const weather_service_snapshot_t *weather,
                        bool temporary_mute, int64_t current_us)
{
    const oled_page_mode_t page = selected_oled_page(
        weather != NULL && weather->available, current_us);

    switch (page) {
    case OLED_PAGE_WATER:
        render_water_oled(care);
        break;
    case OLED_PAGE_ENVIRONMENT:
        render_environment_oled(snapshot);
        break;
    case OLED_PAGE_WEATHER:
        if (weather != NULL) {
            render_weather_oled(weather);
        } else {
            render_weather_oled(&(weather_service_snapshot_t){0});
        }
        break;
    case OLED_PAGE_CARE:
    case OLED_PAGE_AUTO:
    default:
        render_care_oled(snapshot, care, temporary_mute);
        break;
    }
    draw_button_row(snapshot, care, temporary_mute, page);
}

static void refresh_snapshot(wellness_snapshot_t *snapshot,
                             const pir_service_snapshot_t *pir,
                             const sedentary_snapshot_t *sedentary,
                             uint32_t current_ms)
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
    if (ssd1306_is_ready()) {
        snapshot->oled_controller = ssd1306_controller_name();
    } else {
        snapshot->oled_controller =
            s_config.oled_sh1106 ? "sh1106" : "ssd1306";
    }
    snapshot->oled_page = oled_page_name(s_oled_page);
    snapshot->oled_manual_page = s_oled_page != OLED_PAGE_AUTO;

    snapshot->sedentary_active = sedentary->active;
    snapshot->sedentary_present = sedentary->present;
    snapshot->sedentary_alert_active = sedentary->alert_active;
    snapshot->sedentary_phase = sedentary_phase_name(sedentary->phase);
    (void)feedback_service_get_snapshot(&feedback);
    snapshot->beeping = feedback.beeping;
    snapshot->vibrating = feedback.vibrating;
    snapshot->feedback_active = feedback.active;
    snapshot->beep_count = feedback.beep_count;
    snapshot->temporary_mute_active = temporary_mute_active(current_ms);
    snapshot->temporary_mute_remaining_ms =
        snapshot->temporary_mute_active
            ? s_temporary_mute_until_ms - current_ms
            : 0u;
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

static void toggle_temporary_mute(uint32_t current_ms)
{
    if (temporary_mute_active(current_ms)) {
        s_temporary_mute_until_ms = 0u;
        ESP_LOGI(TAG, "temporary mute cleared");
        return;
    }

    s_temporary_mute_until_ms = current_ms + WELLNESS_TEMPORARY_MUTE_MS;
    feedback_service_stop();
    ESP_LOGI(TAG, "temporary mute enabled for 30 minutes");
}

static void cycle_alert_mode(void)
{
    care_snapshot_t current = {0};
    care_settings_t settings;
    care_alert_mode_t next_mode;
    esp_err_t err;

    care_guidance_get_snapshot(&current);
    settings = current.settings;
    next_mode = (care_alert_mode_t)(
        ((unsigned)settings.alert_mode + 1u) %
        ((unsigned)CARE_ALERT_MODE_SILENT + 1u));
    settings.alert_mode = next_mode;
    err = care_guidance_set_settings(&settings);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "alert mode update failed: %s",
                 esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "alert mode set to %s",
             care_alert_mode_name(next_mode));
    (void)feedback_service_play(FEEDBACK_SCENE_CONFIRM);
}

static void handle_water_button(button_event_type_t event)
{
    care_snapshot_t before = {0};
    care_snapshot_t after = {0};
    int32_t delta_ml;
    esp_err_t err;

    care_guidance_get_snapshot(&before);
    if (before.settings.cup_ml == 0u) {
        return;
    }
    delta_ml = event == BUTTON_EVENT_LONG_PRESS
                   ? -(int32_t)before.settings.cup_ml
                   : (int32_t)before.settings.cup_ml;
    err = care_guidance_add_water(delta_ml);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "water key update failed: %s",
                 esp_err_to_name(err));
        return;
    }

    care_guidance_get_snapshot(&after);
    ESP_LOGI(TAG, "water key: %+ld ml, total %lu/%lu ml", (long)delta_ml,
             (unsigned long)after.water_ml,
             (unsigned long)after.water_goal_ml);
    if (after.water_ml != before.water_ml) {
        (void)feedback_service_play(FEEDBACK_SCENE_CONFIRM);
    }
}

static void handle_page_button(button_event_type_t event)
{
    if (event == BUTTON_EVENT_LONG_PRESS) {
        s_oled_page = OLED_PAGE_AUTO;
        ESP_LOGI(TAG, "OLED page set to auto carousel");
        return;
    }

    switch (s_oled_page) {
    case OLED_PAGE_AUTO:
    case OLED_PAGE_WEATHER:
        s_oled_page = OLED_PAGE_CARE;
        break;
    case OLED_PAGE_CARE:
        s_oled_page = OLED_PAGE_WATER;
        break;
    case OLED_PAGE_WATER:
        s_oled_page = OLED_PAGE_ENVIRONMENT;
        break;
    case OLED_PAGE_ENVIRONMENT:
        s_oled_page = OLED_PAGE_WEATHER;
        break;
    default:
        s_oled_page = OLED_PAGE_CARE;
        break;
    }
    ESP_LOGI(TAG, "OLED page set to %s", oled_page_name(s_oled_page));
}

static void handle_button_event(button_id_t button,
                                button_event_type_t event,
                                uint32_t current_ms)
{
    if (event == BUTTON_EVENT_NONE) {
        return;
    }

    switch (button) {
    case BUTTON_ID_ACK:
        if (event == BUTTON_EVENT_SHORT_PRESS) {
            feedback_service_stop();
            ESP_LOGI(TAG, "feedback acknowledged");
        }
        break;
    case BUTTON_ID_WATER:
        handle_water_button(event);
        break;
    case BUTTON_ID_MUTE:
        if (event == BUTTON_EVENT_SHORT_PRESS) {
            toggle_temporary_mute(current_ms);
        } else {
            cycle_alert_mode();
        }
        break;
    case BUTTON_ID_PAGE:
        handle_page_button(event);
        break;
    default:
        break;
    }
}

static void poll_buttons(uint32_t current_ms)
{
    button_service_update_result_t result;

    if (!s_buttons_ready) {
        return;
    }

    button_service_update(&s_buttons, current_ms,
                          button_input_read_mask(&s_button_input), &result);
    for (uint8_t index = 0u; index < BUTTON_SERVICE_COUNT; index++) {
        handle_button_event((button_id_t)index, result.events[index],
                            current_ms);
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

        poll_buttons(current_ms);
        (void)temporary_mute_active(current_ms);
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
            temporary_mute_active(current_ms)
                ? FEEDBACK_MODE_SILENT
                : feedback_mode_from_care(
                      care_snapshot.settings.alert_mode));
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
                .sh1106 = s_config.oled_sh1106,
            };
            esp_err_t err = ssd1306_init(&oled_config);

            last_oled_attempt_us = current_us;
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "OLED %s ready at 0x%02X",
                         ssd1306_controller_name(), ssd1306_address());
                oled_failure_logged = false;
            } else if (!oled_failure_logged) {
                ESP_LOGW(TAG,
                         "OLED not found at 0x3C/0x3D; retrying: %s",
                         esp_err_to_name(err));
                oled_failure_logged = true;
            }
        }

        refresh_snapshot(&snapshot, &pir_result.snapshot,
                         &sedentary_result.snapshot, current_ms);
        publish_snapshot(&snapshot);

        if (snapshot.oled_ready &&
            current_us - last_oled_us >=
                (WELLNESS_OLED_REFRESH_MS * 1000LL)) {
            weather_service_snapshot_t weather_snapshot = {0};

            weather_service_get_snapshot(&weather_snapshot);
            render_oled(&snapshot, &care_snapshot, &weather_snapshot,
                        temporary_mute_active(current_ms), current_us);
            if (ssd1306_flush() != ESP_OK && !oled_failure_logged) {
                ESP_LOGW(TAG, "OLED write failed; retrying");
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

    if (s_config.buttons_enabled) {
        const button_service_config_t button_config = {
            .debounce_ms = s_config.button_debounce_ms,
            .long_press_ms = s_config.button_long_press_ms,
        };

        err = button_input_init(&s_button_input, s_config.button_gpios);
        if (err == ESP_OK) {
            uint8_t pressed_mask =
                button_input_read_mask(&s_button_input);

            button_service_init(&s_buttons, &button_config, now_ms(),
                                pressed_mask);
            s_buttons_ready = true;
            ESP_LOGI(TAG,
                     "keys ready: K1=GPIO%d K2=GPIO%d K3=GPIO%d K4=GPIO%d "
                     "(debounce=%lu ms, long=%lu ms)",
                     s_config.button_gpios[0], s_config.button_gpios[1],
                     s_config.button_gpios[2], s_config.button_gpios[3],
                     (unsigned long)s_config.button_debounce_ms,
                     (unsigned long)s_config.button_long_press_ms);
        } else {
            ESP_LOGW(TAG, "four-key input unavailable: %s",
                     esp_err_to_name(err));
        }
    } else {
        ESP_LOGI(TAG, "four-key input disabled");
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
                  "break=%lu min, OLED=%s",
             (unsigned long)(s_config.sedentary_initial_ms / 60000u),
             (unsigned long)(s_config.sedentary_escalation_ms / 60000u),
             (unsigned long)(s_config.sedentary_break_ms / 60000u),
             s_config.oled_sh1106 ? "SH1106" : "SSD1306");
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
