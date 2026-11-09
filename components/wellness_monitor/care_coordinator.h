#pragma once

#include <stdbool.h>

#include "sedentary_reminder.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CARE_SCENE_NONE = 0,
    CARE_SCENE_WATER_REMINDER,
    CARE_SCENE_SEDENTARY_ALERT,
    CARE_SCENE_SEDENTARY_ESCALATED,
} care_scene_t;

typedef struct {
    care_scene_t scene;
    bool water_event;
} care_coordinator_decision_t;

/*
 * Chooses one primary attention scene. The water event remains visible even
 * when a higher-priority sedentary scene suppresses the water feedback.
 */
void care_coordinator_evaluate(
    const sedentary_update_result_t *sedentary_result,
    bool water_reminder_event,
    care_coordinator_decision_t *decision);

#ifdef __cplusplus
}
#endif
