#include "status_led_pattern.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void boot_pattern_has_three_short_flashes(void)
{
    assert(status_led_pattern_on(STATUS_LED_MODE_BOOTING, 0u));
    assert(status_led_pattern_on(STATUS_LED_MODE_BOOTING, 119u));
    assert(!status_led_pattern_on(STATUS_LED_MODE_BOOTING, 120u));
    assert(status_led_pattern_on(STATUS_LED_MODE_BOOTING, 240u));
    assert(!status_led_pattern_on(STATUS_LED_MODE_BOOTING, 360u));
    assert(status_led_pattern_on(STATUS_LED_MODE_BOOTING, 480u));
    assert(!status_led_pattern_on(STATUS_LED_MODE_BOOTING, 600u));
    assert(!status_led_pattern_on(STATUS_LED_MODE_BOOTING, 1199u));
    assert(status_led_pattern_on(STATUS_LED_MODE_BOOTING, 1200u));
}

static void ready_pattern_is_a_low_duty_heartbeat(void)
{
    assert(status_led_pattern_on(STATUS_LED_MODE_READY, 0u));
    assert(!status_led_pattern_on(STATUS_LED_MODE_READY, 80u));
    assert(!status_led_pattern_on(STATUS_LED_MODE_READY, 1999u));
    assert(status_led_pattern_on(STATUS_LED_MODE_READY, 2000u));
}

static void degraded_pattern_has_two_flashes(void)
{
    assert(status_led_pattern_on(STATUS_LED_MODE_DEGRADED, 0u));
    assert(!status_led_pattern_on(STATUS_LED_MODE_DEGRADED, 120u));
    assert(status_led_pattern_on(STATUS_LED_MODE_DEGRADED, 240u));
    assert(!status_led_pattern_on(STATUS_LED_MODE_DEGRADED, 360u));
}

static void error_pattern_has_three_faster_flashes(void)
{
    assert(status_led_pattern_on(STATUS_LED_MODE_ERROR, 0u));
    assert(status_led_pattern_on(STATUS_LED_MODE_ERROR, 220u));
    assert(status_led_pattern_on(STATUS_LED_MODE_ERROR, 440u));
    assert(!status_led_pattern_on(STATUS_LED_MODE_ERROR, 540u));
}

static void every_mode_is_named_and_bounded(void)
{
    for (int mode = STATUS_LED_MODE_OFF;
         mode < STATUS_LED_MODE_COUNT; mode++) {
        status_led_mode_t value = (status_led_mode_t)mode;

        assert(strcmp(status_led_mode_name(value), "unknown") != 0);
        assert(status_led_pattern_period_ms(value) <= 2000u);
        (void)status_led_pattern_on(value, 0xFFFFFFFFu);
    }
    assert(!status_led_pattern_on(STATUS_LED_MODE_OFF, 0u));
}

int main(void)
{
    boot_pattern_has_three_short_flashes();
    ready_pattern_is_a_low_duty_heartbeat();
    degraded_pattern_has_two_flashes();
    error_pattern_has_three_faster_flashes();
    every_mode_is_named_and_bounded();
    puts("status LED pattern tests passed");
    return 0;
}
