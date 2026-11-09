#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FEEDBACK_PATTERN_MAX_PULSES 4u

typedef struct {
    uint16_t on_ms;
    uint16_t off_ms;
} feedback_pulse_t;

typedef struct {
    const feedback_pulse_t *pulses;
    uint8_t pulse_count;
} feedback_pattern_t;

typedef struct {
    bool active;
    bool output_on;
    uint8_t pulse_count;
    uint8_t pulse_index;
    uint32_t remaining_ms;
} feedback_player_snapshot_t;

typedef struct {
    const feedback_pattern_t *pattern;
    uint64_t started_us;
    uint8_t pulse_index;
    bool active;
    bool output_on;
} feedback_player_t;

uint32_t feedback_pattern_duration_ms(const feedback_pattern_t *pattern);
void feedback_player_start(feedback_player_t *player,
                           const feedback_pattern_t *pattern,
                           uint64_t now_us);
void feedback_player_update(feedback_player_t *player, uint64_t now_us);
void feedback_player_stop(feedback_player_t *player);
void feedback_player_get_snapshot(feedback_player_t *player, uint64_t now_us,
                                  feedback_player_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif
