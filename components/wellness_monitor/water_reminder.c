#include "water_reminder.h"

#include <stddef.h>

void water_reminder_init(water_reminder_t *reminder,
                         uint32_t nag_interval_s)
{
    if (reminder == NULL) {
        return;
    }

    reminder->nag_interval_s = nag_interval_s == 0u ? 1u : nag_interval_s;
    reminder->last_nag_unix = 0;
}

bool water_reminder_update(water_reminder_t *reminder, int64_t unix_time,
                           bool time_valid, bool water_due)
{
    if (reminder == NULL) {
        return false;
    }
    if (!water_due) {
        reminder->last_nag_unix = 0;
        return false;
    }
    if (!time_valid || unix_time <= 0) {
        return false;
    }

    if (reminder->last_nag_unix == 0 ||
        unix_time >=
            reminder->last_nag_unix + (int64_t)reminder->nag_interval_s) {
        reminder->last_nag_unix = unix_time;
        return true;
    }
    return false;
}

void water_reminder_reset(water_reminder_t *reminder)
{
    if (reminder != NULL) {
        reminder->last_nag_unix = 0;
    }
}
