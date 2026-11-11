#include "vibration_patterns_internal.h"

#include <stddef.h>

static const feedback_pulse_t NAVIGATE_PULSES[] = {
    {180u, 0u},
};
static const feedback_pulse_t REFRESH_STARTED_PULSES[] = {
    {180u, 90u},
    {180u, 0u},
};
static const feedback_pulse_t REFRESH_SUCCESS_PULSES[] = {
    {350u, 0u},
};
static const feedback_pulse_t REFRESH_FAILED_PULSES[] = {
    {500u, 200u},
    {500u, 0u},
};
static const feedback_pulse_t PRICE_ALERT_PULSES[] = {
    {700u, 250u},
    {700u, 0u},
};
static const feedback_pulse_t CONFIG_SAVED_PULSES[] = {
    {320u, 0u},
};
static const feedback_pulse_t CONFIG_SAVE_FAILED_PULSES[] = {
    {450u, 180u},
    {450u, 0u},
};
static const feedback_pulse_t WIFI_CONNECTING_PULSES[] = {
    {220u, 110u},
    {220u, 0u},
};
static const feedback_pulse_t WIFI_CONNECTED_PULSES[] = {
    {350u, 130u},
    {350u, 0u},
};
static const feedback_pulse_t WIFI_DISCONNECTED_PULSES[] = {
    {550u, 220u},
    {550u, 0u},
};
static const feedback_pulse_t CLIENT_CONNECTED_PULSES[] = {
    {200u, 0u},
};
static const feedback_pulse_t CLIENT_DISCONNECTED_PULSES[] = {
    {260u, 0u},
};
static const feedback_pulse_t SYSTEM_DEGRADED_PULSES[] = {
    {420u, 180u},
    {420u, 180u},
    {420u, 0u},
};

#define PULSE_PATTERN(name)                                                   \
    {                                                                         \
        name##_PULSES,                                                        \
        (uint8_t)(sizeof(name##_PULSES) / sizeof(name##_PULSES[0]))           \
    }

static const feedback_pattern_t s_patterns[VIBRATION_PATTERN_COUNT] = {
    [VIBRATION_PATTERN_NONE] = {NULL, 0u},
    [VIBRATION_PATTERN_NAVIGATE] = PULSE_PATTERN(NAVIGATE),
    [VIBRATION_PATTERN_REFRESH_STARTED] = PULSE_PATTERN(REFRESH_STARTED),
    [VIBRATION_PATTERN_REFRESH_SUCCESS] = PULSE_PATTERN(REFRESH_SUCCESS),
    [VIBRATION_PATTERN_REFRESH_FAILED] = PULSE_PATTERN(REFRESH_FAILED),
    [VIBRATION_PATTERN_PRICE_ALERT] = PULSE_PATTERN(PRICE_ALERT),
    [VIBRATION_PATTERN_CONFIG_SAVED] = PULSE_PATTERN(CONFIG_SAVED),
    [VIBRATION_PATTERN_CONFIG_SAVE_FAILED] =
        PULSE_PATTERN(CONFIG_SAVE_FAILED),
    [VIBRATION_PATTERN_WIFI_CONNECTING] =
        PULSE_PATTERN(WIFI_CONNECTING),
    [VIBRATION_PATTERN_WIFI_CONNECTED] = PULSE_PATTERN(WIFI_CONNECTED),
    [VIBRATION_PATTERN_WIFI_DISCONNECTED] =
        PULSE_PATTERN(WIFI_DISCONNECTED),
    [VIBRATION_PATTERN_CLIENT_CONNECTED] =
        PULSE_PATTERN(CLIENT_CONNECTED),
    [VIBRATION_PATTERN_CLIENT_DISCONNECTED] =
        PULSE_PATTERN(CLIENT_DISCONNECTED),
    [VIBRATION_PATTERN_SYSTEM_DEGRADED] =
        PULSE_PATTERN(SYSTEM_DEGRADED),
};

const feedback_pattern_t *vibration_pattern_get(vibration_pattern_t pattern)
{
    return vibration_pattern_valid(pattern) ? &s_patterns[pattern] : NULL;
}

bool vibration_pattern_valid(vibration_pattern_t pattern)
{
    return pattern >= VIBRATION_PATTERN_NONE &&
           pattern < VIBRATION_PATTERN_COUNT;
}

uint8_t vibration_pattern_pulse_count(vibration_pattern_t pattern)
{
    const feedback_pattern_t *config = vibration_pattern_get(pattern);

    return config != NULL ? config->pulse_count : 0u;
}

uint32_t vibration_pattern_duration_ms(vibration_pattern_t pattern)
{
    const feedback_pattern_t *config = vibration_pattern_get(pattern);

    return config != NULL ? feedback_pattern_duration_ms(config) : 0u;
}
