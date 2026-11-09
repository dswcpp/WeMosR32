#include "sedentary_reminder.h"

#include <stddef.h>

static bool reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static uint32_t remaining_ms(uint32_t now_ms, uint32_t deadline_ms)
{
    int32_t remaining = (int32_t)(deadline_ms - now_ms);
    return remaining > 0 ? (uint32_t)remaining : 0u;
}

const char *sedentary_phase_name(sedentary_phase_t phase)
{
    switch (phase) {
    case SEDENTARY_PHASE_IDLE:
        return "idle";
    case SEDENTARY_PHASE_SITTING:
        return "sitting";
    case SEDENTARY_PHASE_ON_BREAK:
        return "on_break";
    case SEDENTARY_PHASE_ALERTING:
        return "alerting";
    default:
        return "unknown";
    }
}

const char *sedentary_transition_reason_name(
    sedentary_transition_reason_t reason)
{
    switch (reason) {
    case SEDENTARY_TRANSITION_NONE:
        return "none";
    case SEDENTARY_TRANSITION_MOTION_DETECTED:
        return "motion_detected";
    case SEDENTARY_TRANSITION_ABSENCE_DETECTED:
        return "absence_detected";
    case SEDENTARY_TRANSITION_ALERT_DUE:
        return "alert_due";
    case SEDENTARY_TRANSITION_SHORT_BREAK_RETURN:
        return "short_break_return";
    case SEDENTARY_TRANSITION_BREAK_RETURN:
        return "break_return";
    case SEDENTARY_TRANSITION_BREAK_COMPLETED:
        return "break_completed";
    default:
        return "unknown";
    }
}

static void transition_phase(sedentary_state_t *state, uint32_t now_ms,
                             sedentary_phase_t next,
                             sedentary_transition_reason_t reason,
                             sedentary_update_result_t *result)
{
    sedentary_phase_t previous = state->phase;
    sedentary_transition_t *transition;

    if (previous == next) {
        return;
    }

    state->phase = next;
    if (result->transition_count >= SEDENTARY_MAX_TRANSITIONS_PER_UPDATE) {
        result->transition_overflow = true;
        result->output_count++;
        return;
    }

    transition = &result->transitions[result->transition_count++];
    transition->from = previous;
    transition->to = next;
    transition->reason = reason;
    transition->at_ms = now_ms;
    transition->order = result->output_count++;
}

static void emit_event(const sedentary_state_t *state, uint32_t now_ms,
                       sedentary_update_result_t *result,
                       sedentary_event_t event)
{
    sedentary_event_record_t *record;

    if (result->event_count >= SEDENTARY_MAX_EVENTS_PER_UPDATE) {
        result->event_overflow = true;
        result->output_count++;
        return;
    }

    record = &result->events[result->event_count++];
    record->type = event;
    record->phase = state->phase;
    record->at_ms = now_ms;
    record->order = result->output_count++;
    record->reminder_level = state->reminder_level;
}

static void start_session(sedentary_state_t *state, uint32_t now_ms,
                          sedentary_update_result_t *result)
{
    state->sitting_since_ms = now_ms;
    state->last_motion_ms = now_ms;
    state->next_escalation_ms = 0u;
    state->reminder_level = 0u;
    transition_phase(state, now_ms, SEDENTARY_PHASE_SITTING,
                     SEDENTARY_TRANSITION_MOTION_DETECTED, result);
}

static void clear_session(sedentary_state_t *state, uint32_t now_ms,
                          sedentary_update_result_t *result)
{
    state->sitting_since_ms = now_ms;
    state->last_motion_ms = now_ms;
    state->next_escalation_ms = 0u;
    state->reminder_level = 0u;
    transition_phase(state, now_ms, SEDENTARY_PHASE_IDLE,
                     SEDENTARY_TRANSITION_BREAK_COMPLETED, result);
}

static void enter_alert(sedentary_state_t *state, uint32_t now_ms,
                        sedentary_update_result_t *result)
{
    state->reminder_level = 3u;
    state->next_escalation_ms = now_ms + state->config.escalation_ms;
    state->alert_starts++;
    transition_phase(state, now_ms, SEDENTARY_PHASE_ALERTING,
                     SEDENTARY_TRANSITION_ALERT_DUE, result);
}

static void enter_break(sedentary_state_t *state, uint32_t now_ms,
                        sedentary_update_result_t *result)
{
    transition_phase(state, now_ms, SEDENTARY_PHASE_ON_BREAK,
                     SEDENTARY_TRANSITION_ABSENCE_DETECTED, result);
}

static void complete_break(sedentary_state_t *state, uint32_t now_ms,
                           sedentary_update_result_t *result)
{
    bool was_alerting = state->reminder_level >= 3u;

    clear_session(state, now_ms, result);
    if (was_alerting) {
        state->completed_breaks++;
        emit_event(state, now_ms, result, SEDENTARY_EVENT_BREAK_COMPLETED);
    }
}

static void return_from_break(sedentary_state_t *state, uint32_t now_ms,
                              sedentary_update_result_t *result)
{
    uint32_t absent_ms = now_ms - state->last_motion_ms;

    if (absent_ms >= state->config.break_ms) {
        complete_break(state, now_ms, result);
        start_session(state, now_ms, result);
        return;
    }

    state->last_motion_ms = now_ms;
    if (state->reminder_level >= 3u) {
        state->next_escalation_ms = now_ms + state->config.escalation_ms;
        state->short_breaks++;
        transition_phase(state, now_ms, SEDENTARY_PHASE_ALERTING,
                         SEDENTARY_TRANSITION_SHORT_BREAK_RETURN, result);
        emit_event(state, now_ms, result,
                   SEDENTARY_EVENT_SHORT_BREAK_RETURN);
    } else {
        transition_phase(state, now_ms, SEDENTARY_PHASE_SITTING,
                         SEDENTARY_TRANSITION_BREAK_RETURN, result);
    }
}

static void handle_sitting_deadlines(sedentary_state_t *state, uint32_t now_ms,
                                     sedentary_update_result_t *result)
{
    if (state->reminder_level < 1u &&
        reached(now_ms,
                state->sitting_since_ms + state->config.gentle_nudge_ms)) {
        state->reminder_level = 1u;
        state->gentle_nudges++;
        emit_event(state, now_ms, result, SEDENTARY_EVENT_GENTLE_NUDGE);
    }

    if (state->reminder_level < 2u &&
        reached(now_ms,
                state->sitting_since_ms + state->config.activity_nudge_ms)) {
        state->reminder_level = 2u;
        state->activity_nudges++;
        emit_event(state, now_ms, result, SEDENTARY_EVENT_ACTIVITY_NUDGE);
    }

    if (state->phase != SEDENTARY_PHASE_ALERTING &&
        reached(now_ms,
                state->sitting_since_ms + state->config.alert_after_ms)) {
        enter_alert(state, now_ms, result);
        emit_event(state, now_ms, result, SEDENTARY_EVENT_ALERT_STARTED);
    } else if (state->phase == SEDENTARY_PHASE_ALERTING &&
               reached(now_ms, state->next_escalation_ms)) {
        state->next_escalation_ms =
            now_ms + state->config.escalation_ms;
        state->escalations++;
        emit_event(state, now_ms, result, SEDENTARY_EVENT_ALERT_ESCALATED);
    }
}

static void handle_idle_phase(sedentary_state_t *state, uint32_t now_ms,
                              bool motion_evidence,
                              sedentary_update_result_t *result)
{
    if (motion_evidence) {
        start_session(state, now_ms, result);
    }
}

static void handle_sitting_phase(sedentary_state_t *state, uint32_t now_ms,
                                 bool motion_evidence,
                                 sedentary_update_result_t *result)
{
    if (motion_evidence) {
        state->last_motion_ms = now_ms;
    } else if ((uint32_t)(now_ms - state->last_motion_ms) >=
               state->config.absence_ms) {
        enter_break(state, now_ms, result);
        return;
    }

    handle_sitting_deadlines(state, now_ms, result);
}

static void handle_on_break_phase(sedentary_state_t *state, uint32_t now_ms,
                                  bool motion_evidence,
                                  sedentary_update_result_t *result)
{
    if (motion_evidence) {
        return_from_break(state, now_ms, result);
    } else if ((uint32_t)(now_ms - state->last_motion_ms) >=
               state->config.break_ms) {
        complete_break(state, now_ms, result);
    }
}

static void handle_alerting_phase(sedentary_state_t *state, uint32_t now_ms,
                                  bool motion_evidence,
                                  sedentary_update_result_t *result)
{
    if (motion_evidence) {
        state->last_motion_ms = now_ms;
    } else if ((uint32_t)(now_ms - state->last_motion_ms) >=
               state->config.absence_ms) {
        enter_break(state, now_ms, result);
        return;
    }

    handle_sitting_deadlines(state, now_ms, result);
}

void sedentary_init(sedentary_state_t *state, uint32_t now_ms,
                    const sedentary_config_t *config)
{
    if (state == NULL || config == NULL) {
        return;
    }

    *state = (sedentary_state_t){0};
    state->config = *config;
    state->sitting_since_ms = now_ms;
    state->last_motion_ms = now_ms;
    state->phase = SEDENTARY_PHASE_IDLE;
    state->initialized = true;
}

void sedentary_update(sedentary_state_t *state, uint32_t now_ms,
                      bool motion_evidence,
                      sedentary_update_result_t *result)
{
    if (state == NULL || result == NULL) {
        return;
    }
    *result = (sedentary_update_result_t){0};
    if (!state->initialized) {
        sedentary_init(state, now_ms, &state->config);
    }

    /* Settle transitions caused by the same observation without reordering
     * their emitted events. */
    for (uint8_t step = 0;
         step <= SEDENTARY_MAX_TRANSITIONS_PER_UPDATE; step++) {
        sedentary_phase_t previous = state->phase;

        switch (state->phase) {
        case SEDENTARY_PHASE_IDLE:
            handle_idle_phase(state, now_ms, motion_evidence, result);
            break;
        case SEDENTARY_PHASE_SITTING:
            handle_sitting_phase(state, now_ms, motion_evidence, result);
            break;
        case SEDENTARY_PHASE_ON_BREAK:
            handle_on_break_phase(state, now_ms, motion_evidence, result);
            break;
        case SEDENTARY_PHASE_ALERTING:
            handle_alerting_phase(state, now_ms, motion_evidence, result);
            break;
        }

        if (state->phase == previous) {
            break;
        }
    }

    sedentary_get_snapshot(state, now_ms, &result->snapshot);
}

void sedentary_get_snapshot(const sedentary_state_t *state, uint32_t now_ms,
                            sedentary_snapshot_t *snapshot)
{
    if (state == NULL || snapshot == NULL) {
        return;
    }

    *snapshot = (sedentary_snapshot_t){0};
    snapshot->phase = state->phase;
    snapshot->active = state->phase != SEDENTARY_PHASE_IDLE;
    snapshot->present =
        state->phase == SEDENTARY_PHASE_SITTING ||
        state->phase == SEDENTARY_PHASE_ALERTING;
    snapshot->alert_active =
        state->phase == SEDENTARY_PHASE_ALERTING;
    snapshot->reminder_level = state->reminder_level;
    snapshot->alert_starts = state->alert_starts;
    snapshot->escalations = state->escalations;
    snapshot->gentle_nudges = state->gentle_nudges;
    snapshot->activity_nudges = state->activity_nudges;
    snapshot->short_breaks = state->short_breaks;
    snapshot->completed_breaks = state->completed_breaks;
    if (state->phase == SEDENTARY_PHASE_IDLE) {
        return;
    }

    snapshot->sitting_elapsed_ms = now_ms - state->sitting_since_ms;
    if (state->phase == SEDENTARY_PHASE_ON_BREAK) {
        snapshot->break_remaining_ms = remaining_ms(
            now_ms, state->last_motion_ms + state->config.break_ms);
        return;
    }

    if (state->phase == SEDENTARY_PHASE_ALERTING) {
        snapshot->next_reminder_ms =
            remaining_ms(now_ms, state->next_escalation_ms);
    } else if (state->reminder_level < 1u) {
        snapshot->next_reminder_ms = remaining_ms(
            now_ms, state->sitting_since_ms + state->config.gentle_nudge_ms);
    } else if (state->reminder_level < 2u) {
        snapshot->next_reminder_ms = remaining_ms(
            now_ms, state->sitting_since_ms + state->config.activity_nudge_ms);
    } else {
        snapshot->next_reminder_ms = remaining_ms(
            now_ms, state->sitting_since_ms + state->config.alert_after_ms);
    }
}
