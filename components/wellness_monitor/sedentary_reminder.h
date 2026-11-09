#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t gentle_nudge_ms;
    uint32_t activity_nudge_ms;
    uint32_t alert_after_ms;
    uint32_t escalation_ms;
    uint32_t break_ms;
    uint32_t absence_ms;
} sedentary_config_t;

typedef enum {
    SEDENTARY_EVENT_NONE = 0,
    SEDENTARY_EVENT_GENTLE_NUDGE,
    SEDENTARY_EVENT_ACTIVITY_NUDGE,
    SEDENTARY_EVENT_ALERT_STARTED,
    SEDENTARY_EVENT_ALERT_ESCALATED,
    SEDENTARY_EVENT_SHORT_BREAK_RETURN,
    SEDENTARY_EVENT_BREAK_COMPLETED,
} sedentary_event_t;

typedef enum {
    SEDENTARY_PHASE_IDLE = 0,
    SEDENTARY_PHASE_SITTING,
    SEDENTARY_PHASE_ON_BREAK,
    SEDENTARY_PHASE_ALERTING,
} sedentary_phase_t;

typedef enum {
    SEDENTARY_TRANSITION_NONE = 0,
    SEDENTARY_TRANSITION_MOTION_DETECTED,
    SEDENTARY_TRANSITION_ABSENCE_DETECTED,
    SEDENTARY_TRANSITION_ALERT_DUE,
    SEDENTARY_TRANSITION_SHORT_BREAK_RETURN,
    SEDENTARY_TRANSITION_BREAK_RETURN,
    SEDENTARY_TRANSITION_BREAK_COMPLETED,
} sedentary_transition_reason_t;

#define SEDENTARY_MAX_EVENTS_PER_UPDATE 8u
#define SEDENTARY_MAX_TRANSITIONS_PER_UPDATE 4u

typedef struct {
    sedentary_event_t type;
    sedentary_phase_t phase;
    uint32_t at_ms;
    /* Position in this update's combined transition/event timeline. */
    uint8_t order;
    uint8_t reminder_level;
} sedentary_event_record_t;

typedef struct {
    sedentary_phase_t from;
    sedentary_phase_t to;
    sedentary_transition_reason_t reason;
    uint32_t at_ms;
    /* Position in this update's combined transition/event timeline. */
    uint8_t order;
} sedentary_transition_t;

typedef struct {
    sedentary_phase_t phase;
    uint32_t sitting_elapsed_ms;
    uint32_t next_reminder_ms;
    uint32_t break_remaining_ms;
    uint32_t alert_starts;
    uint32_t escalations;
    uint32_t gentle_nudges;
    uint32_t activity_nudges;
    uint32_t short_breaks;
    uint32_t completed_breaks;
    uint8_t reminder_level;
    bool active;
    bool present;
    bool alert_active;
} sedentary_snapshot_t;

typedef struct {
    /* Occurrences in this update, ordered by order. */
    sedentary_event_record_t events[SEDENTARY_MAX_EVENTS_PER_UPDATE];
    /* Phase changes in this update, ordered by order. */
    sedentary_transition_t transitions[SEDENTARY_MAX_TRANSITIONS_PER_UPDATE];
    /* Current state after all occurrences in this update were applied. */
    sedentary_snapshot_t snapshot;
    uint8_t event_count;
    uint8_t transition_count;
    /* Next value shared by transition and event order fields. */
    uint8_t output_count;
    bool event_overflow;
    bool transition_overflow;
} sedentary_update_result_t;

typedef struct {
    sedentary_config_t config;
    uint32_t sitting_since_ms;
    uint32_t last_motion_ms;
    uint32_t next_escalation_ms;
    uint32_t alert_starts;
    uint32_t escalations;
    uint32_t gentle_nudges;
    uint32_t activity_nudges;
    uint32_t short_breaks;
    uint32_t completed_breaks;
    uint8_t reminder_level;
    sedentary_phase_t phase;
    bool initialized;
} sedentary_state_t;

static inline bool sedentary_snapshot_active(
    const sedentary_snapshot_t *snapshot)
{
    return snapshot != NULL && snapshot->active;
}

static inline bool sedentary_snapshot_present(
    const sedentary_snapshot_t *snapshot)
{
    return snapshot != NULL && snapshot->present;
}

static inline bool sedentary_snapshot_alert_active(
    const sedentary_snapshot_t *snapshot)
{
    return snapshot != NULL && snapshot->alert_active;
}

/* Returns a stable lowercase name for logs and management APIs. */
const char *sedentary_phase_name(sedentary_phase_t phase);
const char *sedentary_transition_reason_name(
    sedentary_transition_reason_t reason);
void sedentary_init(sedentary_state_t *state, uint32_t now_ms,
                    const sedentary_config_t *config);
void sedentary_update(sedentary_state_t *state, uint32_t now_ms,
                      bool motion_evidence,
                      sedentary_update_result_t *result);
void sedentary_get_snapshot(const sedentary_state_t *state, uint32_t now_ms,
                            sedentary_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif
