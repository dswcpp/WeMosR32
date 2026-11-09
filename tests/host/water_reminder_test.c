#include <assert.h>
#include <stdio.h>

#include "water_reminder.h"

static void due_reminder_is_rate_limited(void)
{
    water_reminder_t reminder;

    water_reminder_init(&reminder, 900u);
    assert(water_reminder_update(&reminder, 1000, true, true));
    assert(!water_reminder_update(&reminder, 1899, true, true));
    assert(water_reminder_update(&reminder, 1900, true, true));
}

static void leaving_due_state_resets_the_interval(void)
{
    water_reminder_t reminder;

    water_reminder_init(&reminder, 900u);
    assert(water_reminder_update(&reminder, 1000, true, true));
    assert(!water_reminder_update(&reminder, 1100, true, false));
    assert(water_reminder_update(&reminder, 1200, true, true));
}

static void invalid_time_does_not_consume_a_reminder(void)
{
    water_reminder_t reminder;

    water_reminder_init(&reminder, 900u);
    assert(!water_reminder_update(&reminder, 0, false, true));
    assert(water_reminder_update(&reminder, 1000, true, true));
}

static void reset_allows_an_immediate_reminder(void)
{
    water_reminder_t reminder;

    water_reminder_init(&reminder, 900u);
    assert(water_reminder_update(&reminder, 1000, true, true));
    assert(!water_reminder_update(&reminder, 1100, true, true));
    water_reminder_reset(&reminder);
    assert(water_reminder_update(&reminder, 1100, true, true));
}

int main(void)
{
    due_reminder_is_rate_limited();
    leaving_due_state_resets_the_interval();
    invalid_time_does_not_consume_a_reminder();
    reset_allows_an_immediate_reminder();
    puts("water reminder tests passed");
    return 0;
}
