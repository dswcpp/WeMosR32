#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t nag_interval_s;
    int64_t last_nag_unix;
} water_reminder_t;

void water_reminder_init(water_reminder_t *reminder,
                         uint32_t nag_interval_s);
bool water_reminder_update(water_reminder_t *reminder, int64_t unix_time,
                           bool time_valid, bool water_due);
void water_reminder_reset(water_reminder_t *reminder);

#ifdef __cplusplus
}
#endif
