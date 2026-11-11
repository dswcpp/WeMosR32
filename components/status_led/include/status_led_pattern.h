#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STATUS_LED_MODE_OFF = 0,
    STATUS_LED_MODE_BOOTING,
    STATUS_LED_MODE_READY,
    STATUS_LED_MODE_DEGRADED,
    STATUS_LED_MODE_ERROR,
    STATUS_LED_MODE_COUNT,
} status_led_mode_t;

bool status_led_pattern_on(status_led_mode_t mode, uint32_t elapsed_ms);
uint32_t status_led_pattern_period_ms(status_led_mode_t mode);
const char *status_led_mode_name(status_led_mode_t mode);

#ifdef __cplusplus
}
#endif
