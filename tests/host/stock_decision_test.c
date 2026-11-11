#include "stock_decision.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static stock_quote_t quote_at(double price)
{
    stock_quote_t quote = {0};

    quote.valid = true;
    quote.price = price;
    quote.high = price * 1.02;
    quote.low = price * 0.98;
    return quote;
}

static stock_analysis_snapshot_t analysis_with(
    const stock_quote_t *quote, float rsi, double upper, float volume_ratio)
{
    stock_analysis_snapshot_t analysis = {
        .supported = true,
        .valid = true,
    };
    stock_analyzer_result_t *result = &analysis.live;

    result->ready = STOCK_READY_EMA | STOCK_READY_MACD | STOCK_READY_RSI |
                    STOCK_READY_VWAP | STOCK_READY_BOLL | STOCK_READY_VOLUME;
    result->score = 70;
    result->trend = STOCK_TREND_UP;
    result->ema_fast = (float)quote->price + 0.05f;
    result->ema_slow = (float)quote->price - 0.10f;
    result->macd_histogram = 0.05f;
    result->rsi = rsi;
    result->vwap = (float)(quote->price * 0.995);
    result->boll_middle = (float)(upper - 0.35);
    result->boll_upper = (float)upper;
    result->boll_lower = (float)(upper - 0.70);
    result->volume_ratio = volume_ratio;
    return analysis;
}

static void ready_setup_has_entry_stop_and_reward(void)
{
    stock_quote_t quote = quote_at(10.0);
    stock_analysis_snapshot_t analysis =
        analysis_with(&quote, 58.0f, 10.40, 1.4f);
    stock_decision_t decision;

    stock_decision_evaluate(&quote, &analysis, &decision);

    assert(decision.state == STOCK_DECISION_READY);
    assert(decision.confirmations == 5u);
    assert(decision.entry_low < decision.entry_high);
    assert(decision.entry_low <= quote.price);
    assert(decision.entry_high >= quote.price);
    assert(decision.invalidation < decision.entry_low);
    assert(decision.target > decision.entry_high);
    assert(decision.risk_percent > 1.0);
    assert(decision.risk_percent < 3.0);
    assert(decision.reward_risk >= 2.0);
}

static void overextended_price_waits_for_pullback(void)
{
    stock_quote_t quote = quote_at(10.7);
    stock_analysis_snapshot_t analysis =
        analysis_with(&quote, 76.0f, 10.55, 2.5f);
    stock_decision_t decision;

    analysis.live.vwap = 10.0f;
    stock_decision_evaluate(&quote, &analysis, &decision);

    assert(decision.state == STOCK_DECISION_WAIT);
    assert(decision.entry_high < quote.price);
    assert(decision.invalidation > 0.0);
    assert(decision.target > decision.entry_high);
}

static void bearish_alignment_is_avoided(void)
{
    stock_quote_t quote = quote_at(9.5);
    stock_analysis_snapshot_t analysis =
        analysis_with(&quote, 34.0f, 9.90, 0.7f);
    stock_decision_t decision;

    analysis.live.score = -70;
    analysis.live.trend = STOCK_TREND_DOWN;
    analysis.live.ema_fast = 9.40f;
    analysis.live.ema_slow = 9.65f;
    analysis.live.macd_histogram = -0.08f;
    analysis.live.vwap = 9.75f;

    stock_decision_evaluate(&quote, &analysis, &decision);

    assert(decision.state == STOCK_DECISION_AVOID);
    assert(decision.entry_low == 0.0);
    assert(decision.invalidation == 0.0);
    assert(decision.target == 0.0);
}

static void weak_reward_risk_is_not_ready(void)
{
    stock_quote_t quote = quote_at(10.0);
    stock_analysis_snapshot_t analysis =
        analysis_with(&quote, 58.0f, 10.12, 1.4f);
    stock_decision_t decision;

    stock_decision_evaluate(&quote, &analysis, &decision);

    assert(decision.state == STOCK_DECISION_WAIT);
    assert(decision.reward_risk < 2.0);
}

static void missing_indicators_are_unavailable(void)
{
    stock_quote_t quote = quote_at(10.0);
    stock_analysis_snapshot_t analysis = {
        .supported = true,
        .valid = true,
    };
    stock_decision_t decision;

    stock_decision_evaluate(&quote, &analysis, &decision);
    assert(decision.state == STOCK_DECISION_UNAVAILABLE);
}

static void closed_bar_fills_incomplete_live_fields(void)
{
    stock_quote_t quote = quote_at(10.0);
    stock_analysis_snapshot_t analysis =
        analysis_with(&quote, 58.0f, 10.40, 1.4f);
    stock_analyzer_result_t complete = analysis.live;
    stock_analyzer_result_t effective;

    analysis.has_closed = true;
    analysis.closed = complete;
    analysis.live.ready = STOCK_READY_EMA | STOCK_READY_MACD;

    effective = stock_decision_effective_analysis(&analysis);
    assert((effective.ready & STOCK_READY_RSI) != 0u);
    assert((effective.ready & STOCK_READY_VWAP) != 0u);
    assert((effective.ready & STOCK_READY_BOLL) != 0u);
    assert((effective.ready & STOCK_READY_VOLUME) != 0u);
}

int main(void)
{
    ready_setup_has_entry_stop_and_reward();
    overextended_price_waits_for_pullback();
    bearish_alignment_is_avoided();
    weak_reward_risk_is_not_ready();
    missing_indicators_are_unavailable();
    closed_bar_fills_incomplete_live_fields();
    puts("stock decision tests passed");
    return 0;
}
