#include "stock_decision.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define DECISION_REQUIRED_READY                                              \
    (STOCK_READY_EMA | STOCK_READY_MACD | STOCK_READY_RSI |                \
     STOCK_READY_VWAP | STOCK_READY_BOLL)
#define DECISION_MIN_RISK_PERCENT 0.01
#define DECISION_MAX_RISK_PERCENT 0.03
#define DECISION_MIN_REWARD_RISK 2.0
#define DECISION_ENTRY_BELOW 0.003
#define DECISION_ENTRY_ABOVE 0.006
#define DECISION_MAX_VWAP_DISTANCE 0.020

static bool valid_positive(double value)
{
    return isfinite(value) && value > 0.0;
}

static double clamp_double(double value, double minimum, double maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static void fill_indicator(stock_analyzer_result_t *display,
                           const stock_analyzer_result_t *closed,
                           uint32_t ready_bit)
{
    if ((display->ready & ready_bit) == 0u &&
        (closed->ready & ready_bit) != 0u) {
        display->ready |= ready_bit;
    }
}

stock_analyzer_result_t stock_decision_effective_analysis(
    const stock_analysis_snapshot_t *analysis)
{
    stock_analyzer_result_t display = {0};

    if (analysis == NULL) {
        return display;
    }
    display = analysis->live;
    if (!analysis->has_closed) {
        return display;
    }

    if ((display.ready & STOCK_READY_EMA) == 0u &&
        (analysis->closed.ready & STOCK_READY_EMA) != 0u) {
        display.ema_fast = analysis->closed.ema_fast;
        display.ema_slow = analysis->closed.ema_slow;
    }
    if ((display.ready & STOCK_READY_MACD) == 0u &&
        (analysis->closed.ready & STOCK_READY_MACD) != 0u) {
        display.macd = analysis->closed.macd;
        display.macd_signal = analysis->closed.macd_signal;
        display.macd_histogram = analysis->closed.macd_histogram;
    }
    if ((display.ready & STOCK_READY_RSI) == 0u &&
        (analysis->closed.ready & STOCK_READY_RSI) != 0u) {
        display.rsi = analysis->closed.rsi;
    }
    if ((display.ready & STOCK_READY_VWAP) == 0u &&
        (analysis->closed.ready & STOCK_READY_VWAP) != 0u) {
        display.vwap = analysis->closed.vwap;
    }
    if ((display.ready & STOCK_READY_BOLL) == 0u &&
        (analysis->closed.ready & STOCK_READY_BOLL) != 0u) {
        display.boll_middle = analysis->closed.boll_middle;
        display.boll_lower = analysis->closed.boll_lower;
        display.boll_upper = analysis->closed.boll_upper;
    }
    if ((display.ready & STOCK_READY_VOLUME) == 0u &&
        (analysis->closed.ready & STOCK_READY_VOLUME) != 0u) {
        display.volume_ratio = analysis->closed.volume_ratio;
    }

    fill_indicator(&display, &analysis->closed, STOCK_READY_EMA);
    fill_indicator(&display, &analysis->closed, STOCK_READY_MACD);
    fill_indicator(&display, &analysis->closed, STOCK_READY_RSI);
    fill_indicator(&display, &analysis->closed, STOCK_READY_VWAP);
    fill_indicator(&display, &analysis->closed, STOCK_READY_BOLL);
    fill_indicator(&display, &analysis->closed, STOCK_READY_VOLUME);
    return display;
}

void stock_decision_evaluate(const stock_quote_t *quote,
                             const stock_analysis_snapshot_t *analysis,
                             stock_decision_t *decision)
{
    stock_analyzer_result_t result;
    bool bullish_ema;
    bool bullish_macd;
    bool above_vwap;
    bool healthy_rsi;
    bool volume_confirms;
    bool overextended;
    bool near_entry;
    unsigned confirmations = 0u;
    double entry_mid;
    double band_width;
    double risk_abs;

    if (decision == NULL) {
        return;
    }
    memset(decision, 0, sizeof(*decision));
    decision->state = STOCK_DECISION_UNAVAILABLE;
    decision->confirmation_total = 5u;
    if (quote == NULL || analysis == NULL || !quote->valid ||
        !analysis->valid || !valid_positive(quote->price)) {
        return;
    }

    result = stock_decision_effective_analysis(analysis);
    if ((result.ready & DECISION_REQUIRED_READY) != DECISION_REQUIRED_READY ||
        !valid_positive(result.vwap) ||
        !valid_positive(result.boll_lower) ||
        !valid_positive(result.boll_upper) ||
        result.boll_upper <= result.boll_lower ||
        !isfinite(result.ema_fast) || !isfinite(result.ema_slow) ||
        !isfinite(result.macd_histogram) || !isfinite(result.rsi)) {
        return;
    }

    bullish_ema = result.ema_fast > result.ema_slow;
    bullish_macd = result.macd_histogram > 0.0f;
    above_vwap = quote->price > result.vwap;
    healthy_rsi = result.rsi >= 45.0f && result.rsi <= 68.0f;
    volume_confirms =
        (result.ready & STOCK_READY_VOLUME) != 0u &&
        result.volume_ratio >= 1.2f;

    confirmations += bullish_ema ? 1u : 0u;
    confirmations += bullish_macd ? 1u : 0u;
    confirmations += above_vwap ? 1u : 0u;
    confirmations += healthy_rsi ? 1u : 0u;
    confirmations += volume_confirms ? 1u : 0u;
    decision->confirmations = (uint8_t)confirmations;

    if ((!bullish_ema && !bullish_macd && !above_vwap) ||
        (result.score <= -45 && !bullish_ema && !bullish_macd)) {
        decision->state = STOCK_DECISION_AVOID;
        return;
    }

    decision->entry_low = result.vwap * (1.0 - DECISION_ENTRY_BELOW);
    decision->entry_high = result.vwap * (1.0 + DECISION_ENTRY_ABOVE);
    entry_mid = (decision->entry_low + decision->entry_high) / 2.0;
    band_width = result.boll_upper - result.boll_lower;
    risk_abs = clamp_double(band_width * 0.25,
                            quote->price * DECISION_MIN_RISK_PERCENT,
                            quote->price * DECISION_MAX_RISK_PERCENT);
    decision->invalidation = entry_mid - risk_abs;
    if (decision->invalidation < result.boll_lower * 0.995) {
        decision->invalidation = result.boll_lower * 0.995;
    }
    if (decision->invalidation >= decision->entry_low) {
        decision->invalidation = decision->entry_low * 0.995;
    }

    decision->target =
        fmax(result.boll_upper, fmax(quote->high, quote->price));
    risk_abs = entry_mid - decision->invalidation;
    if (risk_abs > 0.0) {
        decision->risk_percent = risk_abs / entry_mid * 100.0;
        if (decision->target > entry_mid) {
            decision->reward_risk =
                (decision->target - entry_mid) / risk_abs;
        }
    }

    overextended =
        result.rsi >= 70.0f ||
        quote->price >= result.boll_upper * 0.998 ||
        quote->price > result.vwap * (1.0 + DECISION_MAX_VWAP_DISTANCE);
    near_entry = quote->price >= decision->entry_low &&
                 quote->price <= decision->entry_high;
    if (bullish_ema && bullish_macd && above_vwap && healthy_rsi &&
        near_entry && !overextended &&
        decision->reward_risk >= DECISION_MIN_REWARD_RISK) {
        decision->state = STOCK_DECISION_READY;
    } else {
        decision->state = STOCK_DECISION_WAIT;
    }
}
