#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VIBRATION_PATTERN_NONE = 0,
    /* Navigation: one 180 ms haptic tap. */
    VIBRATION_PATTERN_NAVIGATE,
    /* Manual refresh accepted: two 180 ms pulses. */
    VIBRATION_PATTERN_REFRESH_STARTED,
    /* Refresh success: one 350 ms confirmation. */
    VIBRATION_PATTERN_REFRESH_SUCCESS,
    /* Refresh failure: two 500 ms pulses separated by 200 ms. */
    VIBRATION_PATTERN_REFRESH_FAILED,
    /* Price threshold: two 700 ms pulses separated by 250 ms. */
    VIBRATION_PATTERN_PRICE_ALERT,
    /* Configuration saved: one 320 ms confirmation. */
    VIBRATION_PATTERN_CONFIG_SAVED,
    /* Configuration failure: two 450 ms pulses. */
    VIBRATION_PATTERN_CONFIG_SAVE_FAILED,
    /* Wi-Fi connecting: two 220 ms pulses. */
    VIBRATION_PATTERN_WIFI_CONNECTING,
    /* Wi-Fi connected: two 350 ms pulses. */
    VIBRATION_PATTERN_WIFI_CONNECTED,
    /* Wi-Fi disconnected: two 550 ms pulses. */
    VIBRATION_PATTERN_WIFI_DISCONNECTED,
    /* SoftAP client connected: one 200 ms notification. */
    VIBRATION_PATTERN_CLIENT_CONNECTED,
    /* SoftAP client disconnected: one 260 ms notification. */
    VIBRATION_PATTERN_CLIENT_DISCONNECTED,
    /* System degraded: three 420 ms pulses. */
    VIBRATION_PATTERN_SYSTEM_DEGRADED,
    VIBRATION_PATTERN_COUNT,
} vibration_pattern_t;

bool vibration_pattern_valid(vibration_pattern_t pattern);
uint8_t vibration_pattern_pulse_count(vibration_pattern_t pattern);
uint32_t vibration_pattern_duration_ms(vibration_pattern_t pattern);

#ifdef __cplusplus
}
#endif
