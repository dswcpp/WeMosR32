#include "status_led_pattern.h"

#include <stddef.h>

bool status_led_pattern_on(status_led_mode_t mode, uint32_t elapsed_ms)
{
    uint32_t phase;

    switch (mode) {
    case STATUS_LED_MODE_OFF:
        return false;
    case STATUS_LED_MODE_BOOTING:
        phase = elapsed_ms % 1200u;
        return (phase < 120u) ||
               (phase >= 240u && phase < 360u) ||
               (phase >= 480u && phase < 600u);
    case STATUS_LED_MODE_READY:
        phase = elapsed_ms % 2000u;
        return phase < 80u;
    case STATUS_LED_MODE_DEGRADED:
        phase = elapsed_ms % 2000u;
        return (phase < 120u) ||
               (phase >= 240u && phase < 360u);
    case STATUS_LED_MODE_ERROR:
        phase = elapsed_ms % 1600u;
        return (phase < 100u) ||
               (phase >= 220u && phase < 320u) ||
               (phase >= 440u && phase < 540u);
    case STATUS_LED_MODE_COUNT:
    default:
        return false;
    }
}

uint32_t status_led_pattern_period_ms(status_led_mode_t mode)
{
    switch (mode) {
    case STATUS_LED_MODE_BOOTING:
        return 1200u;
    case STATUS_LED_MODE_READY:
    case STATUS_LED_MODE_DEGRADED:
        return 2000u;
    case STATUS_LED_MODE_ERROR:
        return 1600u;
    case STATUS_LED_MODE_OFF:
    case STATUS_LED_MODE_COUNT:
    default:
        return 0u;
    }
}

const char *status_led_mode_name(status_led_mode_t mode)
{
    switch (mode) {
    case STATUS_LED_MODE_OFF:
        return "off";
    case STATUS_LED_MODE_BOOTING:
        return "booting";
    case STATUS_LED_MODE_READY:
        return "ready";
    case STATUS_LED_MODE_DEGRADED:
        return "degraded";
    case STATUS_LED_MODE_ERROR:
        return "error";
    case STATUS_LED_MODE_COUNT:
    default:
        return "unknown";
    }
}
