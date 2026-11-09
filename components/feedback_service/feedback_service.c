#include "feedback_service.h"

#include <stddef.h>

#include "buzzer_service.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "vibration_service.h"

static const char *TAG = "feedback";

static portMUX_TYPE s_mode_mux = portMUX_INITIALIZER_UNLOCKED;
static feedback_mode_t s_mode = FEEDBACK_MODE_BOTH;

static bool mode_valid(feedback_mode_t mode)
{
    return mode >= FEEDBACK_MODE_BOTH && mode <= FEEDBACK_MODE_SILENT;
}

static bool scene_valid(feedback_scene_t scene)
{
    return scene >= FEEDBACK_SCENE_NONE &&
           scene <= FEEDBACK_SCENE_CONFIRM;
}

static buzzer_pattern_t buzzer_pattern_for_scene(feedback_scene_t scene)
{
    switch (scene) {
    case FEEDBACK_SCENE_WATER_REMINDER:
        return BUZZER_PATTERN_WATER_REMINDER;
    case FEEDBACK_SCENE_SEDENTARY_ALERT:
        return BUZZER_PATTERN_SEDENTARY_ALERT;
    case FEEDBACK_SCENE_SEDENTARY_ESCALATED:
        return BUZZER_PATTERN_SEDENTARY_ESCALATED;
    case FEEDBACK_SCENE_CONFIRM:
        return BUZZER_PATTERN_CONFIRM;
    case FEEDBACK_SCENE_NONE:
    default:
        return BUZZER_PATTERN_NONE;
    }
}

static vibration_pattern_t vibration_pattern_for_scene(
    feedback_scene_t scene)
{
    switch (scene) {
    case FEEDBACK_SCENE_WATER_REMINDER:
        return VIBRATION_PATTERN_WATER_REMINDER;
    case FEEDBACK_SCENE_SEDENTARY_ALERT:
        return VIBRATION_PATTERN_SEDENTARY_ALERT;
    case FEEDBACK_SCENE_SEDENTARY_ESCALATED:
        return VIBRATION_PATTERN_SEDENTARY_ESCALATED;
    case FEEDBACK_SCENE_CONFIRM:
        return VIBRATION_PATTERN_CONFIRM;
    case FEEDBACK_SCENE_NONE:
    default:
        return VIBRATION_PATTERN_NONE;
    }
}

esp_err_t feedback_service_start(const feedback_service_config_t *config)
{
    esp_err_t buzzer_err;
    esp_err_t vibration_err;
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

    if (buzzer_err != ESP_OK) {
        return buzzer_err;
    }
    if (vibration_err != ESP_OK) {
        return vibration_err;
    }
    return ESP_OK;
}

esp_err_t feedback_service_set_mode(feedback_mode_t mode)
{
    feedback_mode_t previous;

    if (!mode_valid(mode)) {
        return ESP_ERR_INVALID_ARG;
    }

    taskENTER_CRITICAL(&s_mode_mux);
    previous = s_mode;
    s_mode = mode;
    taskEXIT_CRITICAL(&s_mode_mux);

    if (previous != mode) {
        feedback_service_stop();
    }
    return ESP_OK;
}

esp_err_t feedback_service_play(feedback_scene_t scene)
{
    feedback_mode_t mode;
    esp_err_t first_error = ESP_OK;
    esp_err_t err;

    if (!scene_valid(scene)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (scene == FEEDBACK_SCENE_NONE) {
        feedback_service_stop();
        return ESP_OK;
    }

    taskENTER_CRITICAL(&s_mode_mux);
    mode = s_mode;
    taskEXIT_CRITICAL(&s_mode_mux);
    if (mode == FEEDBACK_MODE_SILENT) {
        feedback_service_stop();
        return ESP_OK;
    }

    if ((mode == FEEDBACK_MODE_BOTH || mode == FEEDBACK_MODE_SOUND) &&
        buzzer_service_is_ready()) {
        err = buzzer_service_play(buzzer_pattern_for_scene(scene));
        if (err != ESP_OK) {
            first_error = err;
        }
    }
    if ((mode == FEEDBACK_MODE_BOTH || mode == FEEDBACK_MODE_VIBRATE) &&
        vibration_service_is_ready()) {
        err = vibration_service_play(
            vibration_pattern_for_scene(scene));
        if (err != ESP_OK && first_error == ESP_OK) {
            first_error = err;
        }
    }
    return first_error;
}

void feedback_service_stop(void)
{
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
