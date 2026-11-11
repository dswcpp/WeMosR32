#include "stock_intraday_seed.h"

#include <math.h>
#include <stddef.h>

bool stock_intraday_seed_analyzer(
    stock_analyzer_t *analyzer, const stock_intraday_sample_t *samples,
    size_t sample_count, uint32_t day_start_utc_s,
    stock_analyzer_result_t *last_result)
{
    stock_analyzer_result_t result;
    bool have_result = false;
    uint32_t previous_timestamp = 0u;

    if (analyzer == NULL || samples == NULL || last_result == NULL ||
        sample_count < 2u) {
        return false;
    }

    for (size_t index = 1u; index < sample_count; index++) {
        const stock_intraday_sample_t *sample = &samples[index];
        const stock_intraday_sample_t *previous = &samples[index - 1u];
        const double volume =
            sample->cumulative_volume - previous->cumulative_volume;
        const double turnover =
            sample->cumulative_turnover - previous->cumulative_turnover;
        const uint32_t timestamp =
            day_start_utc_s + (uint32_t)sample->minute_of_day * 60u;
        stock_analyzer_result_t live;
        stock_tick_t tick;

        if (!isfinite(volume) || volume < 0.0 ||
            !isfinite(turnover) || turnover < 0.0 ||
            (have_result && timestamp <= previous_timestamp)) {
            return false;
        }
        tick.timestamp_s = timestamp;
        tick.price = sample->price;
        tick.volume_delta = (float)volume;
        if (stock_analyzer_feed_tick(analyzer, &tick, &live, NULL) <
            STOCK_ANALYZER_OK) {
            return false;
        }
        previous_timestamp = timestamp;
        have_result = true;
    }

    if (!have_result ||
        stock_analyzer_last_result(analyzer, &result) != STOCK_ANALYZER_OK) {
        return false;
    }
    *last_result = result;
    return true;
}
