#include "care_coordinator.h"

#include <stddef.h>

void care_coordinator_evaluate(
    const sedentary_update_result_t *sedentary_result,
    bool water_reminder_event,
    care_coordinator_decision_t *decision)
{
    if (decision == NULL) {
        return;
    }

    *decision = (care_coordinator_decision_t){
        .scene = CARE_SCENE_NONE,
        .water_event = water_reminder_event,
    };
    if (sedentary_result == NULL) {
        if (water_reminder_event) {
            decision->scene = CARE_SCENE_WATER_REMINDER;
        }
        return;
    }

    /*
     * Attention outputs never stack. Escalation has the highest priority,
     * followed by the first sedentary alert, then a water reminder. Gentle and
     * activity nudges remain screen/log guidance and do not drive feedback.
     */
    for (uint8_t index = 0u;
         index < sedentary_result->event_count; index++) {
        switch (sedentary_result->events[index].type) {
        case SEDENTARY_EVENT_ALERT_ESCALATED:
        case SEDENTARY_EVENT_SHORT_BREAK_RETURN:
            decision->scene = CARE_SCENE_SEDENTARY_ESCALATED;
            return;
        case SEDENTARY_EVENT_ALERT_STARTED:
            decision->scene = CARE_SCENE_SEDENTARY_ALERT;
            break;
        case SEDENTARY_EVENT_NONE:
        case SEDENTARY_EVENT_GENTLE_NUDGE:
        case SEDENTARY_EVENT_ACTIVITY_NUDGE:
        case SEDENTARY_EVENT_BREAK_COMPLETED:
        default:
            break;
        }
    }

    if (decision->scene == CARE_SCENE_NONE && water_reminder_event) {
        decision->scene = CARE_SCENE_WATER_REMINDER;
    }
}
