#include <assert.h>
#include <stdio.h>

#include "feedback_player.h"

static void pattern_steps_follow_on_and_off_windows(void)
{
    const feedback_pulse_t pulses[] = {
        {100u, 200u},
        {150u, 0u},
    };
    const feedback_pattern_t pattern = {
        .pulses = pulses,
        .pulse_count = 2u,
    };
    feedback_player_t player;
    feedback_player_snapshot_t snapshot;

    assert(feedback_pattern_duration_ms(&pattern) == 450u);

    feedback_player_start(&player, &pattern, 0u);
    feedback_player_get_snapshot(&player, 0u, &snapshot);
    assert(snapshot.active);
    assert(snapshot.output_on);
    assert(snapshot.pulse_index == 1u);
    assert(snapshot.remaining_ms == 450u);

    feedback_player_get_snapshot(&player, 100000u, &snapshot);
    assert(snapshot.active);
    assert(!snapshot.output_on);
    assert(snapshot.pulse_index == 1u);

    feedback_player_get_snapshot(&player, 300000u, &snapshot);
    assert(snapshot.active);
    assert(snapshot.output_on);
    assert(snapshot.pulse_index == 2u);

    feedback_player_get_snapshot(&player, 449000u, &snapshot);
    assert(snapshot.active);
    assert(snapshot.output_on);

    feedback_player_get_snapshot(&player, 450000u, &snapshot);
    assert(!snapshot.active);
    assert(!snapshot.output_on);
    assert(snapshot.pulse_index == 0u);
    assert(snapshot.remaining_ms == 0u);
}

static void starting_a_new_pattern_replaces_the_old_pattern(void)
{
    const feedback_pulse_t first_pulses[] = {
        {500u, 0u},
    };
    const feedback_pulse_t second_pulses[] = {
        {80u, 0u},
    };
    const feedback_pattern_t first_pattern = {
        .pulses = first_pulses,
        .pulse_count = 1u,
    };
    const feedback_pattern_t second_pattern = {
        .pulses = second_pulses,
        .pulse_count = 1u,
    };
    feedback_player_t player;
    feedback_player_snapshot_t snapshot;

    feedback_player_start(&player, &first_pattern, 0u);
    feedback_player_get_snapshot(&player, 200000u, &snapshot);
    assert(snapshot.active);
    assert(snapshot.output_on);
    assert(snapshot.remaining_ms == 300u);

    feedback_player_start(&player, &second_pattern, 200000u);
    feedback_player_get_snapshot(&player, 200000u, &snapshot);
    assert(snapshot.active);
    assert(snapshot.output_on);
    assert(snapshot.remaining_ms == 80u);

    feedback_player_get_snapshot(&player, 280000u, &snapshot);
    assert(!snapshot.active);
    assert(!snapshot.output_on);
}

static void stop_turns_the_output_off_immediately(void)
{
    const feedback_pulse_t pulses[] = {
        {350u, 0u},
    };
    const feedback_pattern_t pattern = {
        .pulses = pulses,
        .pulse_count = 1u,
    };
    feedback_player_t player;
    feedback_player_snapshot_t snapshot;

    feedback_player_start(&player, &pattern, 0u);
    feedback_player_get_snapshot(&player, 100000u, &snapshot);
    assert(snapshot.output_on);

    feedback_player_stop(&player);
    feedback_player_get_snapshot(&player, 100000u, &snapshot);
    assert(!snapshot.active);
    assert(!snapshot.output_on);
    assert(snapshot.pulse_count == 0u);
    assert(snapshot.remaining_ms == 0u);
}

int main(void)
{
    pattern_steps_follow_on_and_off_windows();
    starting_a_new_pattern_replaces_the_old_pattern();
    stop_turns_the_output_off_immediately();
    puts("feedback player tests passed");
    return 0;
}
