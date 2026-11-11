#include "stock_intraday_seed.h"

#include <assert.h>
#include <stdio.h>

static void seed_full_day_analysis(void)
{
    stock_intraday_sample_t samples[48] = {0};
    stock_analyzer_t analyzer;
    stock_analyzer_config_t config;
    stock_analyzer_result_t result;
    stock_analyzer_result_t live;
    stock_tick_t tick;
    uint32_t day_start = 0u;
    double cumulative_volume = 1000.0;
    double cumulative_turnover = 5680.0;

    assert(stock_intraday_day_start_utc("20261111", &day_start));
    stock_analyzer_default_config(&config);
    assert(stock_analyzer_init(&analyzer, &config) == STOCK_ANALYZER_OK);

    for (size_t index = 0u; index < 48u; index++) {
        samples[index].minute_of_day =
            (uint16_t)(9u * 60u + 30u + index);
        samples[index].price =
            5.68f + (float)index * 0.01f;
        samples[index].cumulative_volume = cumulative_volume;
        samples[index].cumulative_turnover = cumulative_turnover;
        cumulative_volume += 100.0 + (double)index;
        cumulative_turnover +=
            568.0 + (double)index * 10.0;
    }

    assert(stock_intraday_seed_analyzer(&analyzer, samples, 48u, day_start,
                                        &result));
    assert(result.completed_bars == 46u);
    assert((result.ready & STOCK_READY_EMA) != 0u);
    assert((result.ready & STOCK_READY_MACD) != 0u);
    assert((result.ready & STOCK_READY_RSI) != 0u);
    assert((result.ready & STOCK_READY_VWAP) != 0u);
    assert((result.ready & STOCK_READY_BOLL) != 0u);
    assert((result.ready & STOCK_READY_VOLUME) != 0u);

    tick.timestamp_s =
        day_start + (uint32_t)samples[47].minute_of_day * 60u + 30u;
    tick.price = samples[47].price + 0.01f;
    tick.volume_delta = 50.0f;
    assert(stock_analyzer_feed_tick(&analyzer, &tick, &live, NULL) ==
           STOCK_ANALYZER_OK);
    assert(live.is_closed == 0u);

    assert(stock_analyzer_init(&analyzer, &config) == STOCK_ANALYZER_OK);
    samples[20].cumulative_volume = 1.0;
    assert(!stock_intraday_seed_analyzer(&analyzer, samples, 48u, day_start,
                                         &result));
}

int main(void)
{
    seed_full_day_analysis();
    puts("stock intraday seed tests passed");
    return 0;
}
