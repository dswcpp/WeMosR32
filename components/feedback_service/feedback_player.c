#include "feedback_player.h"

#include <stddef.h>

static uint32_t elapsed_ms(uint64_t now_us, uint64_t started_us)
{
    return (uint32_t)((now_us - started_us) / 1000u);
}

uint32_t feedback_pattern_duration_ms(const feedback_pattern_t *pattern)
{
    uint32_t duration_ms = 0u;

    if (pattern == NULL || pattern->pulses == NULL ||
        pattern->pulse_count > FEEDBACK_PATTERN_MAX_PULSES) {
        return 0u;
    }

    for (uint8_t index = 0u; index < pattern->pulse_count; index++) {
        duration_ms += pattern->pulses[index].on_ms;
        if (index + 1u < pattern->pulse_count) {
            duration_ms += pattern->pulses[index].off_ms;
        }
    }
    return duration_ms;
}

static void evaluate(const feedback_player_t *player, uint64_t now_us,
                     bool *active, bool *output_on, uint8_t *pulse_index)
{
    uint32_t elapsed;
    uint32_t cursor_ms = 0u;

    *active = false;
    *output_on = false;
    *pulse_index = 0u;

    if (player == NULL || player->pattern == NULL ||
        player->pattern->pulses == NULL ||
        player->pattern->pulse_count == 0u ||
        player->pattern->pulse_count > FEEDBACK_PATTERN_MAX_PULSES) {
        return;
    }

    elapsed = elapsed_ms(now_us, player->started_us);
    for (uint8_t index = 0u; index < player->pattern->pulse_count; index++) {
        const feedback_pulse_t *pulse = &player->pattern->pulses[index];
        uint32_t on_end_ms = cursor_ms + pulse->on_ms;

        if (elapsed < on_end_ms) {
            *active = true;
            *output_on = true;
            *pulse_index = (uint8_t)(index + 1u);
            return;
        }
        cursor_ms = on_end_ms;

        if (index + 1u >= player->pattern->pulse_count) {
            return;
        }

        if (elapsed < cursor_ms + pulse->off_ms) {
            *active = true;
            *pulse_index = (uint8_t)(index + 1u);
            return;
        }
        cursor_ms += pulse->off_ms;
    }
}

void feedback_player_start(feedback_player_t *player,
                           const feedback_pattern_t *pattern,
                           uint64_t now_us)
{
    if (player == NULL) {
        return;
    }

    *player = (feedback_player_t) {
        .pattern = pattern,
        .started_us = now_us,
    };
    feedback_player_update(player, now_us);
}

void feedback_player_update(feedback_player_t *player, uint64_t now_us)
{
    if (player == NULL) {
        return;
    }

    evaluate(player, now_us, &player->active, &player->output_on,
             &player->pulse_index);
}

void feedback_player_stop(feedback_player_t *player)
{
    if (player == NULL) {
        return;
    }

    player->pattern = NULL;
    player->started_us = 0u;
    player->pulse_index = 0u;
    player->active = false;
    player->output_on = false;
}

void feedback_player_get_snapshot(feedback_player_t *player, uint64_t now_us,
                                  feedback_player_snapshot_t *snapshot)
{
    uint32_t duration_ms;
    uint32_t elapsed;

    if (snapshot == NULL) {
        return;
    }

    *snapshot = (feedback_player_snapshot_t){0};
    if (player == NULL) {
        return;
    }

    feedback_player_update(player, now_us);
    duration_ms = feedback_pattern_duration_ms(player->pattern);
    elapsed = elapsed_ms(now_us, player->started_us);

    snapshot->active = player->active;
    snapshot->output_on = player->output_on;
    snapshot->pulse_count =
        player->pattern != NULL ? player->pattern->pulse_count : 0u;
    snapshot->pulse_index = player->pulse_index;
    snapshot->remaining_ms =
        elapsed < duration_ms ? duration_ms - elapsed : 0u;
}

#ifdef __cplusplus
}
#endif
