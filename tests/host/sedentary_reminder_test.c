#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "sedentary_reminder.h"

static sedentary_config_t test_config(void)
{
    return (sedentary_config_t) {
        .gentle_nudge_ms = 10u * 60u * 1000u,
        .activity_nudge_ms = 20u * 60u * 1000u,
        .alert_after_ms = 45u * 60u * 1000u,
        .escalation_ms = 5u * 60u * 1000u,
        .break_ms = 5u * 60u * 1000u,
        .absence_ms = 2u * 60u * 1000u,
    };
}

static void detection_motion_starts_sitting_session(void)
{
    sedentary_config_t config = test_config();
    sedentary_state_t state;
    sedentary_update_result_t result;

    sedentary_init(&state, 0u, &config);
    sedentary_update(&state, 20u, true, &result);

    assert(result.event_count == 0u);
    assert(result.transition_count == 1u);
    assert(result.transitions[0].from == SEDENTARY_PHASE_IDLE);
    assert(result.transitions[0].to == SEDENTARY_PHASE_SITTING);
    assert(result.transitions[0].reason ==
           SEDENTARY_TRANSITION_MOTION_DETECTED);
    assert(result.transitions[0].order == 0u);
    assert(result.output_count == 1u);
    assert(result.snapshot.phase == SEDENTARY_PHASE_SITTING);
    assert(result.snapshot.active);
    assert(result.snapshot.present);
}

static void ten_minutes_sitting_emits_gentle_nudge(void)
{
    sedentary_config_t config = test_config();
    sedentary_state_t state;
    sedentary_update_result_t result;

    sedentary_init(&state, 0u, &config);
    sedentary_update(&state, 0u, true, &result);
    sedentary_update(&state, 599000u, true, &result);
    sedentary_update(&state, 600000u, false, &result);

    assert(result.event_count == 1u);
    assert(result.events[0].type == SEDENTARY_EVENT_GENTLE_NUDGE);
    assert(result.events[0].phase == SEDENTARY_PHASE_SITTING);
    assert(result.events[0].reminder_level == 1u);
    assert(result.snapshot.reminder_level == 1u);
}

static void overdue_timers_emit_all_events_in_order(void)
{
    sedentary_config_t config = test_config();
    sedentary_state_t state;
    sedentary_update_result_t result;

    config.absence_ms = config.alert_after_ms + 60000u;
    sedentary_init(&state, 0u, &config);
    sedentary_update(&state, 0u, true, &result);
    sedentary_update(&state, 2700000u, false, &result);

    assert(result.event_count == 3u);
    assert(result.events[0].type == SEDENTARY_EVENT_GENTLE_NUDGE);
    assert(result.events[1].type == SEDENTARY_EVENT_ACTIVITY_NUDGE);
    assert(result.events[2].type == SEDENTARY_EVENT_ALERT_STARTED);
    assert(result.events[0].order == 0u);
    assert(result.events[1].order == 1u);
    assert(result.events[2].phase == SEDENTARY_PHASE_ALERTING);
    assert(result.events[2].reminder_level == 3u);
    assert(result.transition_count == 1u);
    assert(result.transitions[0].from == SEDENTARY_PHASE_SITTING);
    assert(result.transitions[0].to == SEDENTARY_PHASE_ALERTING);
    assert(result.transitions[0].reason ==
           SEDENTARY_TRANSITION_ALERT_DUE);
    assert(result.transitions[0].order == 2u);
    assert(result.events[2].order == 3u);
    assert(result.output_count == 4u);
    assert(result.snapshot.phase == SEDENTARY_PHASE_ALERTING);
    assert(result.snapshot.alert_active);
}

static void short_break_returns_to_alerting(void)
{
    sedentary_config_t config = test_config();
    sedentary_state_t state;
    sedentary_update_result_t result;

    sedentary_init(&state, 0u, &config);
    sedentary_update(&state, 0u, true, &result);
    sedentary_update(&state, 2699000u, true, &result);
    sedentary_update(&state, 2700000u, false, &result);
    sedentary_update(&state, 2820000u, false, &result);
    sedentary_update(&state, 2880000u, true, &result);

    assert(result.event_count == 1u);
    assert(result.events[0].type == SEDENTARY_EVENT_SHORT_BREAK_RETURN);
    assert(result.events[0].phase == SEDENTARY_PHASE_ALERTING);
    assert(result.transition_count == 1u);
    assert(result.transitions[0].from == SEDENTARY_PHASE_ON_BREAK);
    assert(result.transitions[0].to == SEDENTARY_PHASE_ALERTING);
    assert(result.transitions[0].reason ==
           SEDENTARY_TRANSITION_SHORT_BREAK_RETURN);
    assert(result.transitions[0].order == 0u);
    assert(result.events[0].order == 1u);
    assert(result.snapshot.phase == SEDENTARY_PHASE_ALERTING);
}

static void valid_break_after_alert_completes_session(void)
{
    sedentary_config_t config = test_config();
    sedentary_state_t state;
    sedentary_update_result_t result;

    sedentary_init(&state, 0u, &config);
    sedentary_update(&state, 0u, true, &result);
    sedentary_update(&state, 2699000u, true, &result);
    sedentary_update(&state, 2700000u, false, &result);
    sedentary_update(&state, 2820000u, false, &result);
    sedentary_update(&state, 2999000u, false, &result);

    assert(result.event_count == 1u);
    assert(result.events[0].type == SEDENTARY_EVENT_BREAK_COMPLETED);
    assert(result.events[0].phase == SEDENTARY_PHASE_IDLE);
    assert(result.snapshot.completed_breaks == 1u);
    assert(result.transition_count == 1u);
    assert(result.transitions[0].from == SEDENTARY_PHASE_ON_BREAK);
    assert(result.transitions[0].to == SEDENTARY_PHASE_IDLE);
    assert(result.transitions[0].reason ==
           SEDENTARY_TRANSITION_BREAK_COMPLETED);
    assert(result.transitions[0].order == 0u);
    assert(result.events[0].order == 1u);
    assert(result.snapshot.phase == SEDENTARY_PHASE_IDLE);
    assert(!result.snapshot.active);
}

static void returning_after_valid_break_starts_new_session(void)
{
    sedentary_config_t config = test_config();
    sedentary_state_t state;
    sedentary_update_result_t result;

    sedentary_init(&state, 0u, &config);
    sedentary_update(&state, 0u, true, &result);
    sedentary_update(&state, 2699000u, true, &result);
    sedentary_update(&state, 2700000u, false, &result);
    sedentary_update(&state, 2820000u, false, &result);
    sedentary_update(&state, 3000000u, true, &result);

    assert(result.event_count == 1u);
    assert(result.events[0].type == SEDENTARY_EVENT_BREAK_COMPLETED);
    assert(result.snapshot.phase == SEDENTARY_PHASE_SITTING);
    assert(result.snapshot.completed_breaks == 1u);
    assert(result.transition_count == 2u);
    assert(result.transitions[0].from == SEDENTARY_PHASE_ON_BREAK);
    assert(result.transitions[0].to == SEDENTARY_PHASE_IDLE);
    assert(result.transitions[1].from == SEDENTARY_PHASE_IDLE);
    assert(result.transitions[1].to == SEDENTARY_PHASE_SITTING);
    assert(result.transitions[0].reason ==
           SEDENTARY_TRANSITION_BREAK_COMPLETED);
    assert(result.transitions[1].reason ==
           SEDENTARY_TRANSITION_MOTION_DETECTED);
    assert(result.transitions[0].order < result.events[0].order);
    assert(result.events[0].order < result.transitions[1].order);
    assert(result.output_count == 3u);
}

static void sitting_through_alert_escalates(void)
{
    sedentary_config_t config = test_config();
    sedentary_state_t state;
    sedentary_update_result_t result;

    sedentary_init(&state, 0u, &config);
    sedentary_update(&state, 0u, true, &result);
    sedentary_update(&state, 2699000u, true, &result);
    sedentary_update(&state, 2700000u, false, &result);
    sedentary_update(&state, 2800000u, true, &result);
    sedentary_update(&state, 2940000u, true, &result);
    sedentary_update(&state, 3000000u, false, &result);

    assert(result.event_count == 1u);
    assert(result.events[0].type == SEDENTARY_EVENT_ALERT_ESCALATED);
    assert(result.events[0].phase == SEDENTARY_PHASE_ALERTING);
    assert(result.events[0].reminder_level == 3u);
    assert(result.snapshot.escalations == 1u);
}

static void absence_moves_sitting_to_break(void)
{
    sedentary_config_t config = test_config();
    sedentary_state_t state;
    sedentary_update_result_t result;

    sedentary_init(&state, 0u, &config);
    sedentary_update(&state, 0u, true, &result);
    sedentary_update(&state, config.absence_ms, false, &result);

    assert(result.snapshot.phase == SEDENTARY_PHASE_ON_BREAK);
    assert(result.transition_count == 1u);
    assert(result.transitions[0].from == SEDENTARY_PHASE_SITTING);
    assert(result.transitions[0].to == SEDENTARY_PHASE_ON_BREAK);
    assert(result.transitions[0].reason ==
           SEDENTARY_TRANSITION_ABSENCE_DETECTED);
}

static void short_absence_returns_to_sitting(void)
{
    sedentary_config_t config = test_config();
    sedentary_state_t state;
    sedentary_update_result_t result;

    sedentary_init(&state, 0u, &config);
    sedentary_update(&state, 0u, true, &result);
    sedentary_update(&state, config.absence_ms, false, &result);
    sedentary_update(&state, 3u * 60u * 1000u, true, &result);

    assert(result.snapshot.phase == SEDENTARY_PHASE_SITTING);
    assert(result.transition_count == 1u);
    assert(result.transitions[0].from == SEDENTARY_PHASE_ON_BREAK);
    assert(result.transitions[0].to == SEDENTARY_PHASE_SITTING);
    assert(result.transitions[0].reason ==
           SEDENTARY_TRANSITION_BREAK_RETURN);
}

static void state_names_are_stable(void)
{
    assert(strcmp(sedentary_phase_name(SEDENTARY_PHASE_IDLE), "idle") == 0);
    assert(strcmp(sedentary_phase_name(SEDENTARY_PHASE_SITTING),
                  "sitting") == 0);
    assert(strcmp(sedentary_phase_name(SEDENTARY_PHASE_ON_BREAK),
                  "on_break") == 0);
    assert(strcmp(sedentary_phase_name(SEDENTARY_PHASE_ALERTING),
                  "alerting") == 0);
    assert(strcmp(sedentary_phase_name((sedentary_phase_t)99),
                  "unknown") == 0);
    assert(strcmp(sedentary_transition_reason_name(
                      SEDENTARY_TRANSITION_MOTION_DETECTED),
                  "motion_detected") == 0);
    assert(strcmp(sedentary_transition_reason_name(
                      SEDENTARY_TRANSITION_ABSENCE_DETECTED),
                  "absence_detected") == 0);
    assert(strcmp(sedentary_transition_reason_name(
                      SEDENTARY_TRANSITION_ALERT_DUE),
                  "alert_due") == 0);
    assert(strcmp(sedentary_transition_reason_name(
                      SEDENTARY_TRANSITION_SHORT_BREAK_RETURN),
                  "short_break_return") == 0);
    assert(strcmp(sedentary_transition_reason_name(
                      SEDENTARY_TRANSITION_BREAK_RETURN),
                  "break_return") == 0);
    assert(strcmp(sedentary_transition_reason_name(
                      SEDENTARY_TRANSITION_BREAK_COMPLETED),
                  "break_completed") == 0);
    assert(strcmp(sedentary_transition_reason_name(
                      (sedentary_transition_reason_t)99),
                  "unknown") == 0);
}

int main(void)
{
    detection_motion_starts_sitting_session();
    ten_minutes_sitting_emits_gentle_nudge();
    overdue_timers_emit_all_events_in_order();
    short_break_returns_to_alerting();
    valid_break_after_alert_completes_session();
    returning_after_valid_break_starts_new_session();
    sitting_through_alert_escalates();
    absence_moves_sitting_to_break();
    short_absence_returns_to_sitting();
    state_names_are_stable();
    puts("sedentary reminder tests passed");
    return 0;
}
