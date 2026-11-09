#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "care_coordinator.h"

static sedentary_update_result_t result_with_events(
    const sedentary_event_t *events, uint8_t event_count)
{
    sedentary_update_result_t result;

    memset(&result, 0, sizeof(result));
    result.event_count = event_count;
    for (uint8_t index = 0u; index < event_count; index++) {
        result.events[index].type = events[index];
        result.events[index].order = index;
    }
    return result;
}

static void escalated_sedentary_scene_wins_over_water(void)
{
    const sedentary_event_t events[] = {
        SEDENTARY_EVENT_ALERT_STARTED,
        SEDENTARY_EVENT_ALERT_ESCALATED,
    };
    sedentary_update_result_t sedentary =
        result_with_events(events, 2u);
    care_coordinator_decision_t decision;

    care_coordinator_evaluate(&sedentary, true, &decision);

    assert(decision.scene == CARE_SCENE_SEDENTARY_ESCALATED);
    assert(decision.water_event);
}

static void first_sedentary_scene_wins_over_water(void)
{
    const sedentary_event_t events[] = {
        SEDENTARY_EVENT_ALERT_STARTED,
    };
    sedentary_update_result_t sedentary =
        result_with_events(events, 1u);
    care_coordinator_decision_t decision;

    care_coordinator_evaluate(&sedentary, true, &decision);

    assert(decision.scene == CARE_SCENE_SEDENTARY_ALERT);
    assert(decision.water_event);
}

static void water_is_used_when_no_stronger_scene_exists(void)
{
    const sedentary_event_t events[] = {
        SEDENTARY_EVENT_ACTIVITY_NUDGE,
        SEDENTARY_EVENT_BREAK_COMPLETED,
    };
    sedentary_update_result_t sedentary =
        result_with_events(events, 2u);
    care_coordinator_decision_t decision;

    care_coordinator_evaluate(&sedentary, true, &decision);

    assert(decision.scene == CARE_SCENE_WATER_REMINDER);
    assert(decision.water_event);
}

static void soft_nudges_do_not_drive_feedback(void)
{
    const sedentary_event_t events[] = {
        SEDENTARY_EVENT_GENTLE_NUDGE,
        SEDENTARY_EVENT_ACTIVITY_NUDGE,
    };
    sedentary_update_result_t sedentary =
        result_with_events(events, 2u);
    care_coordinator_decision_t decision;

    care_coordinator_evaluate(&sedentary, false, &decision);

    assert(decision.scene == CARE_SCENE_NONE);
    assert(!decision.water_event);
}

static void missing_sedentary_result_still_allows_water(void)
{
    care_coordinator_decision_t decision;

    care_coordinator_evaluate(NULL, true, &decision);

    assert(decision.scene == CARE_SCENE_WATER_REMINDER);
    assert(decision.water_event);
}

int main(void)
{
    escalated_sedentary_scene_wins_over_water();
    first_sedentary_scene_wins_over_water();
    water_is_used_when_no_stronger_scene_exists();
    soft_nudges_do_not_drive_feedback();
    missing_sedentary_result_still_allows_water();
    puts("care coordinator tests passed");
    return 0;
}
