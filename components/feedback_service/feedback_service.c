#include "feedback_service.h"

#include <stddef.h>

#include "app_events.h"
#include "buzzer_service.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "vibration_service.h"

static const char *TAG = "feedback";

static portMUX_TYPE s_state_mux = portMUX_INITIALIZER_UNLOCKED;
static feedback_mode_t s_mode = FEEDBACK_MODE_BOTH;
static bool s_mode_initialized;
static feedback_scene_t s_pending_scene;
static feedback_scene_t s_active_scene;
static uint8_t s_active_priority;
static uint32_t s_active_started_ms;
static uint32_t s_active_duration_ms;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool mode_valid(feedback_mode_t mode)
{
    return mode >= FEEDBACK_MODE_BOTH && mode <= FEEDBACK_MODE_SILENT;
}

static bool scene_valid(feedback_scene_t scene)
{
    return scene >= FEEDBACK_SCENE_NONE && scene < FEEDBACK_SCENE_COUNT;
}

static uint8_t scene_priority(feedback_scene_t scene)
{
    switch (scene) {
    case FEEDBACK_SCENE_PRICE_ALERT:
        return 90u;
    case FEEDBACK_SCENE_REFRESH_FAILED:
        return 80u;
    case FEEDBACK_SCENE_CONFIG_SAVE_FAILED:
        return 75u;
    case FEEDBACK_SCENE_WIFI_DISCONNECTED:
        return 70u;
    case FEEDBACK_SCENE_SYSTEM_DEGRADED:
        return 65u;
    case FEEDBACK_SCENE_REFRESH_SUCCESS:
        return 60u;
    case FEEDBACK_SCENE_REFRESH_STARTED:
        return 55u;
    case FEEDBACK_SCENE_WIFI_CONNECTED:
        return 50u;
    case FEEDBACK_SCENE_WIFI_CONNECTING:
        return 45u;
    case FEEDBACK_SCENE_CONFIG_SAVED:
        return 40u;
    case FEEDBACK_SCENE_CLIENT_CONNECTED:
        return 20u;
    case FEEDBACK_SCENE_CLIENT_DISCONNECTED:
        return 15u;
    case FEEDBACK_SCENE_NAVIGATE:
        return 10u;
    case FEEDBACK_SCENE_NONE:
    default:
        return 0u;
    }
}

static buzzer_pattern_t buzzer_pattern_for_scene(feedback_scene_t scene)
{
    switch (scene) {
    case FEEDBACK_SCENE_NAVIGATE:
    case FEEDBACK_SCENE_REFRESH_STARTED:
    case FEEDBACK_SCENE_WIFI_CONNECTING:
    case FEEDBACK_SCENE_CLIENT_CONNECTED:
    case FEEDBACK_SCENE_CLIENT_DISCONNECTED:
        return BUZZER_PATTERN_NAVIGATE;
    case FEEDBACK_SCENE_REFRESH_SUCCESS:
    case FEEDBACK_SCENE_CONFIG_SAVED:
    case FEEDBACK_SCENE_WIFI_CONNECTED:
        return BUZZER_PATTERN_REFRESH_SUCCESS;
    case FEEDBACK_SCENE_REFRESH_FAILED:
    case FEEDBACK_SCENE_CONFIG_SAVE_FAILED:
    case FEEDBACK_SCENE_WIFI_DISCONNECTED:
    case FEEDBACK_SCENE_SYSTEM_DEGRADED:
        return BUZZER_PATTERN_REFRESH_FAILED;
    case FEEDBACK_SCENE_PRICE_ALERT:
        return BUZZER_PATTERN_PRICE_ALERT;
    case FEEDBACK_SCENE_NONE:
    default:
        return BUZZER_PATTERN_NONE;
    }
}

static vibration_pattern_t vibration_pattern_for_scene(
    feedback_scene_t scene)
{
    switch (scene) {
    case FEEDBACK_SCENE_NAVIGATE:
        return VIBRATION_PATTERN_NAVIGATE;
    case FEEDBACK_SCENE_REFRESH_STARTED:
        return VIBRATION_PATTERN_REFRESH_STARTED;
    case FEEDBACK_SCENE_REFRESH_SUCCESS:
        return VIBRATION_PATTERN_REFRESH_SUCCESS;
    case FEEDBACK_SCENE_REFRESH_FAILED:
        return VIBRATION_PATTERN_REFRESH_FAILED;
    case FEEDBACK_SCENE_PRICE_ALERT:
        return VIBRATION_PATTERN_PRICE_ALERT;
    case FEEDBACK_SCENE_CONFIG_SAVED:
        return VIBRATION_PATTERN_CONFIG_SAVED;
    case FEEDBACK_SCENE_CONFIG_SAVE_FAILED:
        return VIBRATION_PATTERN_CONFIG_SAVE_FAILED;
    case FEEDBACK_SCENE_WIFI_CONNECTING:
        return VIBRATION_PATTERN_WIFI_CONNECTING;
    case FEEDBACK_SCENE_WIFI_CONNECTED:
        return VIBRATION_PATTERN_WIFI_CONNECTED;
    case FEEDBACK_SCENE_WIFI_DISCONNECTED:
        return VIBRATION_PATTERN_WIFI_DISCONNECTED;
    case FEEDBACK_SCENE_CLIENT_CONNECTED:
        return VIBRATION_PATTERN_CLIENT_CONNECTED;
    case FEEDBACK_SCENE_CLIENT_DISCONNECTED:
        return VIBRATION_PATTERN_CLIENT_DISCONNECTED;
    case FEEDBACK_SCENE_SYSTEM_DEGRADED:
        return VIBRATION_PATTERN_SYSTEM_DEGRADED;
    case FEEDBACK_SCENE_NONE:
    default:
        return VIBRATION_PATTERN_NONE;
    }
}

static feedback_scene_t scene_for_event(const app_event_t *event)
{
    if (event == NULL) {
        return FEEDBACK_SCENE_NONE;
    }

    switch (event->type) {
    case APP_EVENT_STOCK_REFRESHED:
        return FEEDBACK_SCENE_REFRESH_SUCCESS;
    case APP_EVENT_STOCK_REFRESH_FAILED:
        return FEEDBACK_SCENE_REFRESH_FAILED;
    case APP_EVENT_STOCK_REFRESH_STARTED:
        return FEEDBACK_SCENE_REFRESH_STARTED;
    case APP_EVENT_STOCK_PRICE_ALERT:
        return FEEDBACK_SCENE_PRICE_ALERT;
    case APP_EVENT_STOCK_VIEW_CHANGED:
        return FEEDBACK_SCENE_NAVIGATE;
    case APP_EVENT_WIFI_STATION_CONNECTING:
        return FEEDBACK_SCENE_WIFI_CONNECTING;
    case APP_EVENT_WIFI_STATION_CONNECTED:
        return FEEDBACK_SCENE_WIFI_CONNECTED;
    case APP_EVENT_WIFI_STATION_DISCONNECTED:
        return FEEDBACK_SCENE_WIFI_DISCONNECTED;
    case APP_EVENT_WIFI_CLIENT_CONNECTED:
        return FEEDBACK_SCENE_CLIENT_CONNECTED;
    case APP_EVENT_WIFI_CLIENT_DISCONNECTED:
        return FEEDBACK_SCENE_CLIENT_DISCONNECTED;
    case APP_EVENT_CONFIG_UPDATED:
        return FEEDBACK_SCENE_CONFIG_SAVED;
    case APP_EVENT_CONFIG_SAVE_FAILED:
        return FEEDBACK_SCENE_CONFIG_SAVE_FAILED;
    case APP_EVENT_SYSTEM_DEGRADED:
        return FEEDBACK_SCENE_SYSTEM_DEGRADED;
    case APP_EVENT_NONE:
    default:
        return FEEDBACK_SCENE_NONE;
    }
}

static void on_app_event(const app_event_t *event, void *context)
{
    feedback_scene_t scene = scene_for_event(event);

    (void)context;
    if (scene != FEEDBACK_SCENE_NONE) {
        (void)feedback_service_play(scene);
    }
}

esp_err_t feedback_service_start(const feedback_service_config_t *config)
{
    esp_err_t buzzer_err;
    esp_err_t vibration_err;
    esp_err_t subscribe_err;
    const buzzer_service_config_t buzzer_config = {
        .enabled = config != NULL && config->buzzer_enabled,
        .gpio = config != NULL ? config->buzzer_gpio : -1,
        .active_high = true,
    };
    const vibration_service_config_t vibration_config = {
        .enabled = config != NULL && config->vibration_enabled,
        .gpio = config != NULL ? config->vibration_gpio : -1,
        .active_high = true,
    };

    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    buzzer_err = buzzer_service_start(&buzzer_config);
    if (buzzer_err != ESP_OK) {
        ESP_LOGW(TAG, "buzzer unavailable: %s",
                 esp_err_to_name(buzzer_err));
    }
    vibration_err = vibration_service_start(&vibration_config);
    if (vibration_err != ESP_OK) {
        ESP_LOGW(TAG, "vibration unavailable: %s",
                 esp_err_to_name(vibration_err));
    }

    subscribe_err = app_events_subscribe(on_app_event, NULL);
    if (subscribe_err != ESP_OK) {
        ESP_LOGW(TAG, "event subscription unavailable: %s",
                 esp_err_to_name(subscribe_err));
    }

    if (buzzer_err != ESP_OK) {
        return buzzer_err;
    }
    if (vibration_err != ESP_OK) {
        return vibration_err;
    }
    return subscribe_err;
}

esp_err_t feedback_service_set_mode(feedback_mode_t mode)
{
    feedback_mode_t previous;
    feedback_scene_t pending = FEEDBACK_SCENE_NONE;

    if (!mode_valid(mode)) {
        return ESP_ERR_INVALID_ARG;
    }

    taskENTER_CRITICAL(&s_state_mux);
    previous = s_mode;
    s_mode = mode;
    if (!s_mode_initialized) {
        s_mode_initialized = true;
        pending = s_pending_scene;
        s_pending_scene = FEEDBACK_SCENE_NONE;
    }
    taskEXIT_CRITICAL(&s_state_mux);

    if (previous != mode) {
        feedback_service_stop();
    }
    if (pending != FEEDBACK_SCENE_NONE) {
        (void)feedback_service_play(pending);
    }
    return ESP_OK;
}

esp_err_t feedback_service_play(feedback_scene_t scene)
{
    feedback_mode_t mode;
    buzzer_pattern_t buzzer_pattern;
    vibration_pattern_t vibration_pattern;
    uint32_t duration_ms = 0u;
    uint32_t buzzer_duration_ms;
    uint32_t vibration_duration_ms;
    uint8_t priority = scene_priority(scene);
    uint32_t current_ms;
    bool sound_ready;
    bool vibration_ready;
    esp_err_t first_error = ESP_OK;
    esp_err_t err;

    if (!scene_valid(scene)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (scene == FEEDBACK_SCENE_NONE) {
        feedback_service_stop();
        return ESP_OK;
    }

    taskENTER_CRITICAL(&s_state_mux);
    mode = s_mode;
    if (!s_mode_initialized) {
        if (s_pending_scene == FEEDBACK_SCENE_NONE ||
            priority >= scene_priority(s_pending_scene)) {
            s_pending_scene = scene;
        }
        taskEXIT_CRITICAL(&s_state_mux);
        return ESP_OK;
    }
    taskEXIT_CRITICAL(&s_state_mux);
    if (mode == FEEDBACK_MODE_SILENT) {
        feedback_service_stop();
        return ESP_OK;
    }

    sound_ready = (mode == FEEDBACK_MODE_BOTH ||
                   mode == FEEDBACK_MODE_SOUND) &&
                  buzzer_service_is_ready();
    vibration_ready = (mode == FEEDBACK_MODE_BOTH ||
                       mode == FEEDBACK_MODE_VIBRATE) &&
                      vibration_service_is_ready();
    if (!sound_ready && !vibration_ready) {
        return ESP_OK;
    }

    buzzer_pattern = buzzer_pattern_for_scene(scene);
    vibration_pattern = vibration_pattern_for_scene(scene);
    buzzer_duration_ms =
        buzzer_service_pattern_duration_ms(buzzer_pattern);
    vibration_duration_ms =
        vibration_service_pattern_duration_ms(vibration_pattern);
    if (sound_ready && buzzer_duration_ms > duration_ms) {
        duration_ms = buzzer_duration_ms;
    }
    if (vibration_ready && vibration_duration_ms > duration_ms) {
        duration_ms = vibration_duration_ms;
    }
    if (duration_ms == 0u) {
        return ESP_ERR_INVALID_STATE;
    }

    current_ms = now_ms();
    taskENTER_CRITICAL(&s_state_mux);
    if (s_active_scene != FEEDBACK_SCENE_NONE &&
        (uint32_t)(current_ms - s_active_started_ms) <
            s_active_duration_ms &&
        priority < s_active_priority) {
        taskEXIT_CRITICAL(&s_state_mux);
        return ESP_OK;
    }
    s_active_scene = scene;
    s_active_priority = priority;
    s_active_started_ms = current_ms;
    s_active_duration_ms = duration_ms;
    taskEXIT_CRITICAL(&s_state_mux);

    if (sound_ready) {
        err = buzzer_service_play(buzzer_pattern);
        if (err != ESP_OK) {
            first_error = err;
        }
    }
    if (vibration_ready) {
        err = vibration_service_play(vibration_pattern);
        if (err != ESP_OK && first_error == ESP_OK) {
            first_error = err;
        }
    }
    return first_error;
}

void feedback_service_stop(void)
{
    taskENTER_CRITICAL(&s_state_mux);
    s_active_scene = FEEDBACK_SCENE_NONE;
    s_active_priority = 0u;
    s_active_started_ms = 0u;
    s_active_duration_ms = 0u;
    taskEXIT_CRITICAL(&s_state_mux);

    buzzer_service_stop();
    vibration_service_stop();
}

bool feedback_service_get_snapshot(feedback_service_snapshot_t *snapshot)
{
    buzzer_service_snapshot_t buzzer = {0};
    vibration_service_snapshot_t vibration = {0};

    if (snapshot == NULL) {
        return false;
    }

    *snapshot = (feedback_service_snapshot_t){0};
    snapshot->buzzer_ready = buzzer_service_get_snapshot(&buzzer);
    snapshot->vibration_ready =
        vibration_service_get_snapshot(&vibration);
    if (!snapshot->buzzer_ready && !snapshot->vibration_ready) {
        return false;
    }

    snapshot->active = buzzer.active || vibration.active;
    snapshot->beeping = buzzer.beeping;
    snapshot->vibrating = vibration.vibrating;
    snapshot->beep_count = buzzer.beep_count;
    snapshot->vibration_pulse_count = vibration.pulse_count;
    snapshot->remaining_ms =
        buzzer.remaining_ms > vibration.remaining_ms
            ? buzzer.remaining_ms
            : vibration.remaining_ms;
    return true;
}
