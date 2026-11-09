#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FEEDBACK_MODE_BOTH = 0,
    FEEDBACK_MODE_VIBRATE,
    FEEDBACK_MODE_SOUND,
    FEEDBACK_MODE_SILENT,
} feedback_mode_t;

typedef enum {
    FEEDBACK_SCENE_NONE = 0,
    /* Buzzer: 2x120 ms; vibration: 2x220 ms. */
    FEEDBACK_SCENE_WATER_REMINDER,
    /* Buzzer: 2x160 ms; vibration: 2x300 ms. */
    FEEDBACK_SCENE_SEDENTARY_ALERT,
    /* Buzzer: 3x160 ms; vibration: 3x350 ms. */
    FEEDBACK_SCENE_SEDENTARY_ESCALATED,
    /* Buzzer: 1x80 ms; vibration: 1x100 ms. */
    FEEDBACK_SCENE_CONFIRM,
} feedback_scene_t;

typedef struct {
    bool buzzer_enabled;
    int buzzer_gpio;
    bool vibration_enabled;
    int vibration_gpio;
} feedback_service_config_t;

typedef struct {
    bool buzzer_ready;
    bool vibration_ready;
    bool active;
    bool beeping;
    bool vibrating;
    uint8_t beep_count;
    uint8_t vibration_pulse_count;
    uint32_t remaining_ms;
} feedback_service_snapshot_t;

esp_err_t feedback_service_start(const feedback_service_config_t *config);
esp_err_t feedback_service_set_mode(feedback_mode_t mode);
esp_err_t feedback_service_play(feedback_scene_t scene);
void feedback_service_stop(void);
bool feedback_service_get_snapshot(feedback_service_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif
