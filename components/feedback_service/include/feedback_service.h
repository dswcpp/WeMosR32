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
    /* Haptic tap; sound is a short click. */
    FEEDBACK_SCENE_NAVIGATE,
    /* Manual refresh accepted; vibration: 2x180 ms. */
    FEEDBACK_SCENE_REFRESH_STARTED,
    /* Vibration: one 350 ms confirmation. */
    FEEDBACK_SCENE_REFRESH_SUCCESS,
    /* Vibration: 2x500 ms separated by 200 ms. */
    FEEDBACK_SCENE_REFRESH_FAILED,
    /* Vibration: 2x700 ms separated by 250 ms. */
    FEEDBACK_SCENE_PRICE_ALERT,
    /* Vibration: one 320 ms confirmation. */
    FEEDBACK_SCENE_CONFIG_SAVED,
    /* Vibration: 2x450 ms separated by 180 ms. */
    FEEDBACK_SCENE_CONFIG_SAVE_FAILED,
    /* Vibration: 2x220 ms separated by 110 ms. */
    FEEDBACK_SCENE_WIFI_CONNECTING,
    /* Vibration: 2x350 ms separated by 130 ms. */
    FEEDBACK_SCENE_WIFI_CONNECTED,
    /* Vibration: 2x550 ms separated by 220 ms. */
    FEEDBACK_SCENE_WIFI_DISCONNECTED,
    /* Vibration: one 200 ms notification. */
    FEEDBACK_SCENE_CLIENT_CONNECTED,
    /* Vibration: one 260 ms notification. */
    FEEDBACK_SCENE_CLIENT_DISCONNECTED,
    /* Vibration: 3x420 ms with two 180 ms gaps. */
    FEEDBACK_SCENE_SYSTEM_DEGRADED,
    FEEDBACK_SCENE_COUNT,
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
