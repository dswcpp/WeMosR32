#include "vibration_patterns.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void every_pattern_has_a_distinct_duration(void)
{
    uint32_t durations[VIBRATION_PATTERN_COUNT] = {0};

    assert(vibration_pattern_valid(VIBRATION_PATTERN_NONE));
    assert(vibration_pattern_duration_ms(VIBRATION_PATTERN_NONE) == 0u);
    assert(vibration_pattern_pulse_count(VIBRATION_PATTERN_NONE) == 0u);

    for (int pattern = VIBRATION_PATTERN_NAVIGATE;
         pattern < VIBRATION_PATTERN_COUNT; pattern++) {
        vibration_pattern_t current = (vibration_pattern_t)pattern;

        assert(vibration_pattern_valid(current));
        assert(vibration_pattern_pulse_count(current) > 0u);
        assert(vibration_pattern_pulse_count(current) <= 4u);
        durations[pattern] = vibration_pattern_duration_ms(current);
        assert(durations[pattern] > 0u);
        for (int previous = VIBRATION_PATTERN_NAVIGATE;
             previous < pattern; previous++) {
            assert(durations[previous] != durations[pattern]);
        }
    }
}

static void durations_match_phone_style_feedback(void)
{
    uint32_t navigate =
        vibration_pattern_duration_ms(VIBRATION_PATTERN_NAVIGATE);
    uint32_t success =
        vibration_pattern_duration_ms(VIBRATION_PATTERN_REFRESH_SUCCESS);
    uint32_t failure =
        vibration_pattern_duration_ms(VIBRATION_PATTERN_REFRESH_FAILED);
    uint32_t price =
        vibration_pattern_duration_ms(VIBRATION_PATTERN_PRICE_ALERT);
    uint32_t degraded =
        vibration_pattern_duration_ms(VIBRATION_PATTERN_SYSTEM_DEGRADED);

    assert(navigate >= 150u);
    assert(success >= 300u);
    assert(failure >= 1000u);
    assert(price >= 1500u);
    assert(degraded >= 1400u);
    assert(price > failure);
    assert(failure > success);
    assert(vibration_pattern_pulse_count(VIBRATION_PATTERN_PRICE_ALERT) ==
           2u);
    assert(vibration_pattern_pulse_count(VIBRATION_PATTERN_SYSTEM_DEGRADED) ==
           3u);
}

int main(void)
{
    every_pattern_has_a_distinct_duration();
    durations_match_phone_style_feedback();
    puts("vibration pattern tests passed");
    return 0;
}
