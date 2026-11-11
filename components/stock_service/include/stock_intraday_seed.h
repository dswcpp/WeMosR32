#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "stock_analyzer.h"
#include "stock_intraday_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Replays chronological minute samples into a fresh analyzer. Each sample is
 * treated as a one-minute bar, with cumulative volume/turnover converted to
 * per-minute deltas. The newest sample remains the live partial bar so the
 * analyzer can keep accepting real-time ticks after the replay.
 */
bool stock_intraday_seed_analyzer(
    stock_analyzer_t *analyzer, const stock_intraday_sample_t *samples,
    size_t sample_count, uint32_t day_start_utc_s,
    stock_analyzer_result_t *last_result);

#ifdef __cplusplus
}
#endif
