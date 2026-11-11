#include "stock_analyzer.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static stock_analyzer_t c;
static stock_analyzer_result_t out;
static stock_analyzer_config_t cfg;

static stock_bar_t mkbar(unsigned i, float price, float volume)
{
    stock_bar_t b;
    b.timestamp_s = 1800000000u + i * 60u;
    b.open = price;
    b.high = price + 0.1f;
    b.low = price - 0.1f;
    b.close = price;
    b.volume = volume;
    b.turnover = price * volume;
    return b;
}

static void test_config(void)
{
    stock_analyzer_default_config(&cfg);
    assert(stock_analyzer_init(&c, &cfg) == STOCK_ANALYZER_OK);
    cfg.ema_fast_period = 33u;
    assert(stock_analyzer_init(&c, &cfg) == STOCK_ANALYZER_ERR_CONFIG);
    stock_analyzer_default_config(&cfg);
    assert(stock_analyzer_init(&c, &cfg) == STOCK_ANALYZER_OK);
}

static void test_unchanged_and_reset(void)
{
    unsigned i;
    test_config();
    for (i = 0; i < 40u; ++i) {
        stock_bar_t b = mkbar(i, 10.0f, 100.0f);
        assert(stock_analyzer_push_bar(&c, &b, &out) == STOCK_ANALYZER_OK);
        if (i < 19u) assert(!(out.ready & STOCK_READY_EMA));
        if (i >= 19u) assert(out.ready & STOCK_READY_EMA);
        if (i < 14u) assert(!(out.ready & STOCK_READY_RSI));
        if (i >= 14u) {
            assert(out.ready & STOCK_READY_RSI);
            assert(fabsf(out.rsi - 50.0f) < 1e-4f);
        }
        if (i < 33u) assert(!(out.ready & STOCK_READY_MACD));
        if (i >= 33u) assert(out.ready & STOCK_READY_MACD);
    }
    assert(out.trend == STOCK_TREND_SIDEWAYS);
    assert((out.ready & STOCK_READY_BOLL) != 0u);
    assert(fabsf(out.boll_middle - 10.0f) < 1e-4f);
    assert(fabsf(out.boll_upper - 10.0f) < 1e-4f);
    assert((out.ready & STOCK_READY_BREAKOUT) != 0u);
    assert((out.ready & STOCK_READY_VOLUME) != 0u);
    assert(fabsf(out.volume_ratio - 1.0f) < 1e-4f);
    assert(stock_analyzer_reset_session(&c) == STOCK_ANALYZER_OK);
    {
        stock_bar_t b = mkbar(40u, 10.0f, 100.0f);
        assert(stock_analyzer_push_bar(&c, &b, &out) == STOCK_ANALYZER_OK);
        assert(fabsf(out.vwap - 10.0f) < 1e-4f);
        assert(out.completed_bars == 41u); /* indicators retained */
    }
    assert(stock_analyzer_reset_all(&c) == STOCK_ANALYZER_OK);
    assert(stock_analyzer_last_result(&c, &out) == STOCK_ANALYZER_ERR_ARGUMENT);
}

static void test_trends_volume_and_order(void)
{
    unsigned i;
    stock_bar_t b;
    test_config();
    for (i = 0u; i < 40u; ++i) {
        b = mkbar(i, 20.0f + i * 0.2f, 100.0f);
        assert(stock_analyzer_push_bar(&c, &b, &out) == STOCK_ANALYZER_OK);
    }
    assert(out.trend == STOCK_TREND_UP);
    assert(out.ready & STOCK_READY_MACD);
    assert(out.rsi > 99.0f);
    b = mkbar(40u, 35.0f, 500.0f);
    assert(stock_analyzer_push_bar(&c, &b, &out) == STOCK_ANALYZER_OK);
    assert(out.signals & STOCK_SIGNAL_VOLUME_SPIKE);
    assert(out.signals & STOCK_SIGNAL_BREAKOUT_UP);
    assert(out.volume_ratio > 4.0f);
    assert(stock_analyzer_push_bar(&c, &b, &out) == STOCK_ANALYZER_ERR_TIME);
    b.high = 20.0f; /* invalid high is below open/close */
    b.timestamp_s += 60u;
    assert(stock_analyzer_push_bar(&c, &b, &out) == STOCK_ANALYZER_ERR_ARGUMENT);
    assert(stock_analyzer_last_result(&c, &out) == STOCK_ANALYZER_OK);
    assert(out.completed_bars == 41u);
}

static void test_tick(void)
{
    stock_tick_t t;
    stock_analyzer_result_t live;
    stock_analyzer_result_t closed;
    test_config();
    t.timestamp_s = 1800000000u;
    t.price = 10.0f;
    t.volume_delta = 100.0f;
    assert(stock_analyzer_feed_tick(&c, &t, &live, &closed) == STOCK_ANALYZER_OK);
    assert(live.is_closed == 0u);
    assert(live.completed_bars == 0u); /* only finalized bars count */
    assert(c.completed_bars == 0u);
    t.timestamp_s += 5u;
    t.price = 12.0f;
    t.volume_delta = 200.0f;
    assert(stock_analyzer_feed_tick(&c, &t, &live, &closed) == STOCK_ANALYZER_OK);
    assert(c.completed_bars == 0u);
    t.timestamp_s += 60u;
    t.price = 11.0f;
    t.volume_delta = 50.0f;
    assert(stock_analyzer_feed_tick(&c, &t, &live, &closed) == STOCK_ANALYZER_BAR_COMPLETED);
    assert(closed.is_closed == 1u);
    assert(closed.completed_bars == 1u);
    assert(fabsf(closed.close - 12.0f) < 1e-4f);
    assert(fabsf(closed.vwap - (10.0f*100.0f+12.0f*200.0f)/300.0f) < 0.0001f);
    assert(live.is_closed == 0u);
    assert(c.completed_bars == 1u);
    t.timestamp_s -= 1u;
    assert(stock_analyzer_feed_tick(&c, &t, &live, &closed) == STOCK_ANALYZER_ERR_TIME);
    assert(stock_analyzer_flush_tick(&c, &closed) == STOCK_ANALYZER_BAR_COMPLETED);
    assert(closed.completed_bars == 2u);
    assert(stock_analyzer_flush_tick(&c, &closed) == STOCK_ANALYZER_OK);
    assert(stock_analyzer_push_bar(&c, &(stock_bar_t){0}, &out) == STOCK_ANALYZER_ERR_MODE);
}

static void test_no_volume(void)
{
    stock_bar_t b;
    test_config();
    b = mkbar(0u, 10.0f, 0.0f);
    assert(stock_analyzer_push_bar(&c, &b, &out) == STOCK_ANALYZER_OK);
    assert(!(out.ready & STOCK_READY_VWAP));
    b = mkbar(1u, 12.0f, 100.0f);
    b.turnover = 0.0f; /* fallback HLC3 */
    assert(stock_analyzer_push_bar(&c, &b, &out) == STOCK_ANALYZER_OK);
    assert(out.ready & STOCK_READY_VWAP);
    assert(fabsf(out.vwap - 12.0f) < 1e-4f);
}

int main(void)
{
    test_unchanged_and_reset();
    test_trends_volume_and_order();
    test_no_volume();
    /* test_tick in fresh state */
    test_tick();
    puts("ALL TESTS PASSED");
    return 0;
}
