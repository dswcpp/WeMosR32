#pragma once

#include "care_guidance.h"
#include "sedentary_reminder.h"

void care_guidance_tick(uint32_t uptime_ms, int64_t unix_time,
                        bool time_valid, bool sitting, bool present,
                        uint32_t current_sitting_seconds,
                        const sedentary_update_result_t *sedentary_result,
                        care_event_t *care_event);
