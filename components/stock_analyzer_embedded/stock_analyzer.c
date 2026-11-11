#include "stock_analyzer.h"

#include <math.h>
#include <string.h>

#define SIGNAL_ADD_IF(out, condition, bit) \
    do { if (condition) { (out)->signals |= (bit); } } while (0)

static int valid_float(float x)
{
    return isfinite(x) != 0;
}

static int valid_period(uint8_t p)
{
    return p >= 2u && p <= STOCK_ANALYZER_MAX_WINDOW;
}

static int valid_config(const stock_analyzer_config_t *cfg)
{
    return cfg &&
           valid_period(cfg->ema_fast_period) &&
           valid_period(cfg->ema_slow_period) &&
           cfg->ema_fast_period < cfg->ema_slow_period &&
           valid_period(cfg->macd_fast_period) &&
           valid_period(cfg->macd_slow_period) &&
           cfg->macd_fast_period < cfg->macd_slow_period &&
           valid_period(cfg->macd_signal_period) &&
           valid_period(cfg->rsi_period) &&
           valid_period(cfg->boll_period) &&
           valid_period(cfg->volume_period) &&
           valid_period(cfg->breakout_period) &&
           cfg->bar_seconds > 0u &&
           valid_float(cfg->boll_stddev) && cfg->boll_stddev > 0.0f &&
           valid_float(cfg->volume_spike_ratio) &&
           cfg->volume_spike_ratio > 1.0f;
}

static int valid_bar(const stock_bar_t *bar)
{
    if (!bar || !valid_float(bar->open) || !valid_float(bar->high) ||
        !valid_float(bar->low) || !valid_float(bar->close) ||
        !valid_float(bar->volume) || !valid_float(bar->turnover)) {
        return 0;
    }
    if (bar->low <= 0.0f || bar->volume < 0.0f || bar->turnover < 0.0f ||
        (bar->volume == 0.0f && bar->turnover != 0.0f)) {
        return 0;
    }
    return bar->high >= bar->low &&
           bar->high >= bar->open && bar->high >= bar->close &&
           bar->low <= bar->open && bar->low <= bar->close;
}

static float ema_step(float prev, float value, uint8_t period)
{
    const float a = 2.0f / ((float)period + 1.0f);
    return prev + a * (value - prev);
}

/* i==0 means newest completed bar (before next insertion). */
static unsigned history_index(const stock_analyzer_t *ctx, unsigned i)
{
    return (unsigned)(ctx->history_next + STOCK_ANALYZER_MAX_WINDOW
                     - 1u - i) % STOCK_ANALYZER_MAX_WINDOW;
}

static void history_append(stock_analyzer_t *ctx, const stock_bar_t *bar)
{
    const unsigned n = ctx->history_next;
    ctx->close_history[n] = bar->close;
    ctx->high_history[n] = bar->high;
    ctx->low_history[n] = bar->low;
    ctx->volume_history[n] = bar->volume;
    ctx->history_next = (uint8_t)((n + 1u) % STOCK_ANALYZER_MAX_WINDOW);
    if (ctx->history_count < STOCK_ANALYZER_MAX_WINDOW) {
        ++ctx->history_count;
    }
}

void stock_analyzer_default_config(stock_analyzer_config_t *cfg)
{
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->ema_fast_period = 5u;
    cfg->ema_slow_period = 20u;
    cfg->macd_fast_period = 12u;
    cfg->macd_slow_period = 26u;
    cfg->macd_signal_period = 9u;
    cfg->rsi_period = 14u;
    cfg->boll_period = 20u;
    cfg->volume_period = 20u;
    cfg->breakout_period = 20u;
    cfg->boll_stddev = 2.0f;
    cfg->volume_spike_ratio = 1.8f;
    cfg->bar_seconds = 60u;
}

stock_analyzer_status_t stock_analyzer_init(
    stock_analyzer_t *ctx, const stock_analyzer_config_t *cfg)
{
    if (!ctx) return STOCK_ANALYZER_ERR_ARGUMENT;
    if (!valid_config(cfg)) return STOCK_ANALYZER_ERR_CONFIG;
    memset(ctx, 0, sizeof(*ctx));
    ctx->cfg = *cfg;
    ctx->initialized = 1u;
    return STOCK_ANALYZER_OK;
}

static void assign_score_and_trend(stock_analyzer_result_t *result,
                                   const stock_bar_t *bar)
{
    int score = 0;
    if (result->ready & STOCK_READY_EMA)
        score += result->ema_fast > result->ema_slow ? 35 :
                 result->ema_fast < result->ema_slow ? -35 : 0;
    if (result->ready & STOCK_READY_MACD)
        score += result->macd_histogram > 0.0f ? 20 :
                 result->macd_histogram < 0.0f ? -20 : 0;
    if (result->ready & STOCK_READY_VWAP)
        score += bar->close > result->vwap ? 20 :
                 bar->close < result->vwap ? -20 : 0;
    if (result->signals & STOCK_SIGNAL_BREAKOUT_UP) score += 15;
    if (result->signals & STOCK_SIGNAL_BREAKOUT_DOWN) score -= 15;
    if (result->ready & STOCK_READY_RSI)
        score += result->rsi > 55.0f ? 10 : result->rsi < 45.0f ? -10 : 0;
    if (result->signals & STOCK_SIGNAL_VOLUME_SPIKE)
        score += bar->close > bar->open ? 10 :
                 bar->close < bar->open ? -10 : 0;
    if (score > 100) score = 100;
    if (score < -100) score = -100;
    result->score = (int16_t)score;
    result->trend = !(result->ready & STOCK_READY_EMA) ? STOCK_TREND_UNKNOWN
                    : score >= 45 ? STOCK_TREND_UP
                    : score <= -45 ? STOCK_TREND_DOWN
                    : STOCK_TREND_SIDEWAYS;
}

static stock_analyzer_status_t calculate_bar(
    stock_analyzer_t *ctx, const stock_bar_t *bar,
    stock_analyzer_result_t *out)
{
    float old_volume_mean = 0.0f;
    float old_highest = 0.0f;
    float old_lowest = 0.0f;
    float gain = 0.0f;
    float loss = 0.0f;
    int volume_ready = 0;
    int breakout_ready = 0;
    unsigned i;
    stock_analyzer_result_t result;
    memset(&result, 0, sizeof(result));

    if (!valid_bar(bar)) return STOCK_ANALYZER_ERR_ARGUMENT;
    if (ctx->completed_bars > 0u &&
        bar->timestamp_s <= ctx->last_bar_start_s) {
        return STOCK_ANALYZER_ERR_TIME;
    }

    /* Compare against PRIOR completed bars, not the bar being inserted. */
    if (ctx->history_count >= ctx->cfg.volume_period) {
        double sum = 0.0;
        for (i = 0u; i < ctx->cfg.volume_period; ++i) {
            sum += ctx->volume_history[history_index(ctx, i)];
        }
        old_volume_mean = (float)(sum / ctx->cfg.volume_period);
        volume_ready = (old_volume_mean > 0.0f);
    }
    if (ctx->history_count >= ctx->cfg.breakout_period) {
        old_highest = -INFINITY;
        old_lowest = INFINITY;
        for (i = 0u; i < ctx->cfg.breakout_period; ++i) {
            const unsigned idx = history_index(ctx, i);
            if (ctx->high_history[idx] > old_highest)
                old_highest = ctx->high_history[idx];
            if (ctx->low_history[idx] < old_lowest)
                old_lowest = ctx->low_history[idx];
        }
        breakout_ready = 1;
    }

    if (ctx->completed_bars == 0u) {
        ctx->ema_fast = bar->close;
        ctx->ema_slow = bar->close;
        ctx->macd_fast_ema = bar->close;
        ctx->macd_slow_ema = bar->close;
    } else {
        ctx->ema_fast = ema_step(ctx->ema_fast, bar->close,
                                 ctx->cfg.ema_fast_period);
        ctx->ema_slow = ema_step(ctx->ema_slow, bar->close,
                                 ctx->cfg.ema_slow_period);
        ctx->macd_fast_ema = ema_step(ctx->macd_fast_ema, bar->close,
                                      ctx->cfg.macd_fast_period);
        ctx->macd_slow_ema = ema_step(ctx->macd_slow_ema, bar->close,
                                      ctx->cfg.macd_slow_period);
    }

    if (ctx->has_previous_close) {
        const float diff = bar->close - ctx->previous_close;
        gain = diff > 0.0f ? diff : 0.0f;
        loss = diff < 0.0f ? -diff : 0.0f;
        if (ctx->rsi_seed_count < ctx->cfg.rsi_period) {
            ctx->rsi_avg_gain += gain;
            ctx->rsi_avg_loss += loss;
            ++ctx->rsi_seed_count;
            if (ctx->rsi_seed_count == ctx->cfg.rsi_period) {
                ctx->rsi_avg_gain /= (float)ctx->cfg.rsi_period;
                ctx->rsi_avg_loss /= (float)ctx->cfg.rsi_period;
            }
        } else {
            const float p = (float)ctx->cfg.rsi_period;
            ctx->rsi_avg_gain = (ctx->rsi_avg_gain * (p - 1.0f) + gain) / p;
            ctx->rsi_avg_loss = (ctx->rsi_avg_loss * (p - 1.0f) + loss) / p;
        }
    }
    ctx->previous_close = bar->close;
    ctx->has_previous_close = 1u;

    /* Exact VWAP when turnover is supplied; otherwise HLC3 approximation. */
    if (bar->volume > 0.0f) {
        const double turnover = bar->turnover > 0.0f
            ? (double)bar->turnover
            : ((double)bar->high + bar->low + bar->close) / 3.0 * bar->volume;
        ctx->session_turnover += turnover;
        ctx->session_volume += bar->volume;
    }

    history_append(ctx, bar);
    ++ctx->completed_bars;
    ctx->last_bar_start_s = bar->timestamp_s;

    result.bar_start_s = bar->timestamp_s;
    result.completed_bars = ctx->completed_bars;
    result.is_closed = 1u;
    result.close = bar->close;
    result.ema_fast = ctx->ema_fast;
    result.ema_slow = ctx->ema_slow;
    if (ctx->completed_bars >= ctx->cfg.ema_slow_period) {
        result.ready |= STOCK_READY_EMA;
        if (result.ema_fast > result.ema_slow)
            result.signals |= STOCK_SIGNAL_EMA_BULL;
        else if (result.ema_fast < result.ema_slow)
            result.signals |= STOCK_SIGNAL_EMA_BEAR;
    }

    result.macd = ctx->macd_fast_ema - ctx->macd_slow_ema;
    if (ctx->completed_bars >= ctx->cfg.macd_slow_period) {
        if (ctx->macd_signal_count == 0u) {
            ctx->macd_signal_ema = result.macd;
        } else {
            ctx->macd_signal_ema = ema_step(
                ctx->macd_signal_ema, result.macd, ctx->cfg.macd_signal_period);
        }
        if (ctx->macd_signal_count < ctx->cfg.macd_signal_period)
            ++ctx->macd_signal_count;
        result.macd_signal = ctx->macd_signal_ema;
        result.macd_histogram = result.macd - result.macd_signal;
        if (ctx->macd_signal_count >= ctx->cfg.macd_signal_period) {
            result.ready |= STOCK_READY_MACD;
            if (ctx->has_previous_macd) {
                SIGNAL_ADD_IF(&result,
                    ctx->previous_macd_histogram <= 0.0f &&
                    result.macd_histogram > 0.0f, STOCK_SIGNAL_MACD_CROSS_UP);
                SIGNAL_ADD_IF(&result,
                    ctx->previous_macd_histogram >= 0.0f &&
                    result.macd_histogram < 0.0f, STOCK_SIGNAL_MACD_CROSS_DN);
            }
            ctx->previous_macd_histogram = result.macd_histogram;
            ctx->has_previous_macd = 1u;
        }
    }

    if (ctx->rsi_seed_count >= ctx->cfg.rsi_period) {
        const float g = ctx->rsi_avg_gain;
        const float l = ctx->rsi_avg_loss;
        result.rsi = (g == 0.0f && l == 0.0f) ? 50.0f
                   : l == 0.0f ? 100.0f
                   : 100.0f - 100.0f / (1.0f + g / l);
        result.ready |= STOCK_READY_RSI;
        SIGNAL_ADD_IF(&result, result.rsi >= 70.0f, STOCK_SIGNAL_RSI_HIGH);
        SIGNAL_ADD_IF(&result, result.rsi <= 30.0f, STOCK_SIGNAL_RSI_LOW);
    }

    if (ctx->session_volume > 0.0) {
        result.vwap = (float)(ctx->session_turnover / ctx->session_volume);
        result.ready |= STOCK_READY_VWAP;
        SIGNAL_ADD_IF(&result, bar->close > result.vwap, STOCK_SIGNAL_ABOVE_VWAP);
        SIGNAL_ADD_IF(&result, bar->close < result.vwap, STOCK_SIGNAL_BELOW_VWAP);
    }

    if (ctx->history_count >= ctx->cfg.boll_period) {
        double sum = 0.0;
        double variance = 0.0;
        double mean;
        for (i = 0u; i < ctx->cfg.boll_period; ++i)
            sum += ctx->close_history[history_index(ctx, i)];
        mean = sum / ctx->cfg.boll_period;
        for (i = 0u; i < ctx->cfg.boll_period; ++i) {
            const double d = ctx->close_history[history_index(ctx, i)] - mean;
            variance += d * d;
        }
        variance /= ctx->cfg.boll_period;
        result.boll_middle = (float)mean;
        result.boll_upper = (float)(mean + ctx->cfg.boll_stddev * sqrt(variance));
        result.boll_lower = (float)(mean - ctx->cfg.boll_stddev * sqrt(variance));
        result.ready |= STOCK_READY_BOLL;
        SIGNAL_ADD_IF(&result, bar->close > result.boll_upper, STOCK_SIGNAL_ABOVE_BOLL);
        SIGNAL_ADD_IF(&result, bar->close < result.boll_lower, STOCK_SIGNAL_BELOW_BOLL);
    }

    if (volume_ready) {
        result.volume_ratio = bar->volume / old_volume_mean;
        result.ready |= STOCK_READY_VOLUME;
        SIGNAL_ADD_IF(&result, result.volume_ratio >= ctx->cfg.volume_spike_ratio,
                      STOCK_SIGNAL_VOLUME_SPIKE);
    }
    if (breakout_ready) {
        result.ready |= STOCK_READY_BREAKOUT;
        SIGNAL_ADD_IF(&result, bar->close > old_highest, STOCK_SIGNAL_BREAKOUT_UP);
        SIGNAL_ADD_IF(&result, bar->close < old_lowest, STOCK_SIGNAL_BREAKOUT_DOWN);
    }

    /* Transparent heuristic score, not a calibrated future probability. */
    assign_score_and_trend(&result, bar);

    ctx->last_result = result;
    ctx->has_last_result = 1u;
    if (out) *out = result;
    return STOCK_ANALYZER_OK;
}

stock_analyzer_status_t stock_analyzer_push_bar(
    stock_analyzer_t *ctx, const stock_bar_t *bar,
    stock_analyzer_result_t *out)
{
    stock_analyzer_status_t st;
    if (!ctx || !ctx->initialized) return STOCK_ANALYZER_ERR_ARGUMENT;
    if (ctx->mode == 2u) return STOCK_ANALYZER_ERR_MODE;
    st = calculate_bar(ctx, bar, out);
    if (st == STOCK_ANALYZER_OK) ctx->mode = 1u;
    return st;
}

/* Preview by replaying ONE incomplete candle on a local copy. No original
 * indicator state is mutated and no dynamic memory is allocated. */
static stock_analyzer_status_t preview_partial(
    const stock_analyzer_t *ctx, stock_analyzer_result_t *out)
{
    stock_analyzer_t scratch;
    stock_analyzer_status_t st;
    if (!out) return STOCK_ANALYZER_OK;
    scratch = *ctx;
    st = calculate_bar(&scratch, &ctx->partial_bar, out);
    if (st != STOCK_ANALYZER_OK) return st;
    out->is_closed = 0u;
    out->completed_bars = ctx->completed_bars; /* only finalized bars count */
    /* Intrabar volume is incomplete; suppress close-only volume alert. */
    out->ready &= ~STOCK_READY_VOLUME;
    out->signals &= ~STOCK_SIGNAL_VOLUME_SPIKE;
    out->volume_ratio = 0.0f;
    /* Re-score from provisional signals after excluding incomplete volume. */
    assign_score_and_trend(out, &ctx->partial_bar);
    return STOCK_ANALYZER_OK;
}

stock_analyzer_status_t stock_analyzer_feed_tick(
    stock_analyzer_t *ctx, const stock_tick_t *tick,
    stock_analyzer_result_t *live_out,
    stock_analyzer_result_t *closed_out)
{
    stock_analyzer_status_t st = STOCK_ANALYZER_OK;
    uint32_t bucket;
    float turnover;
    if (!ctx || !ctx->initialized || !tick ||
        !valid_float(tick->price) || tick->price <= 0.0f ||
        !valid_float(tick->volume_delta) || tick->volume_delta < 0.0f) {
        return STOCK_ANALYZER_ERR_ARGUMENT;
    }
    if (ctx->mode == 1u) return STOCK_ANALYZER_ERR_MODE;
    if (ctx->has_partial_bar && tick->timestamp_s < ctx->last_tick_s)
        return STOCK_ANALYZER_ERR_TIME;
    turnover = tick->price * tick->volume_delta;
    if (!valid_float(turnover)) return STOCK_ANALYZER_ERR_ARGUMENT;
    bucket = (tick->timestamp_s / ctx->cfg.bar_seconds) * ctx->cfg.bar_seconds;
    if (ctx->has_partial_bar && bucket < ctx->partial_bar.timestamp_s)
        return STOCK_ANALYZER_ERR_TIME;
    if (!ctx->has_partial_bar && ctx->completed_bars > 0u &&
        bucket <= ctx->last_bar_start_s) return STOCK_ANALYZER_ERR_TIME;

    if (ctx->has_partial_bar && bucket > ctx->partial_bar.timestamp_s) {
        /* This call closes the old bar, without inventing intermediate bars. */
        const stock_analyzer_status_t close_st =
            calculate_bar(ctx, &ctx->partial_bar, closed_out);
        if (close_st != STOCK_ANALYZER_OK) return close_st;
        ctx->has_partial_bar = 0u;
        st = STOCK_ANALYZER_BAR_COMPLETED;
    }
    if (!ctx->has_partial_bar) {
        ctx->partial_bar.timestamp_s = bucket;
        ctx->partial_bar.open = tick->price;
        ctx->partial_bar.high = tick->price;
        ctx->partial_bar.low = tick->price;
        ctx->partial_bar.close = tick->price;
        ctx->partial_bar.volume = tick->volume_delta;
        ctx->partial_bar.turnover = turnover;
        ctx->has_partial_bar = 1u;
    } else {
        stock_bar_t *b = &ctx->partial_bar;
        const float new_volume = b->volume + tick->volume_delta;
        const float new_turnover = b->turnover + turnover;
        if (!valid_float(new_volume) || !valid_float(new_turnover))
            return STOCK_ANALYZER_ERR_ARGUMENT;
        if (tick->price > b->high) b->high = tick->price;
        if (tick->price < b->low) b->low = tick->price;
        b->close = tick->price;
        b->volume = new_volume;
        b->turnover = new_turnover;
    }
    ctx->last_tick_s = tick->timestamp_s;
    ctx->mode = 2u;
    if (live_out) {
        const stock_analyzer_status_t p = preview_partial(ctx, live_out);
        if (p != STOCK_ANALYZER_OK) return p;
    }
    return st;
}

stock_analyzer_status_t stock_analyzer_flush_tick(
    stock_analyzer_t *ctx, stock_analyzer_result_t *closed_out)
{
    stock_analyzer_status_t st;
    if (!ctx || !ctx->initialized) return STOCK_ANALYZER_ERR_ARGUMENT;
    if (ctx->mode == 1u) return STOCK_ANALYZER_ERR_MODE;
    if (!ctx->has_partial_bar) return STOCK_ANALYZER_OK;
    st = calculate_bar(ctx, &ctx->partial_bar, closed_out);
    if (st != STOCK_ANALYZER_OK) return st;
    ctx->has_partial_bar = 0u;
    return STOCK_ANALYZER_BAR_COMPLETED;
}

stock_analyzer_status_t stock_analyzer_last_result(
    const stock_analyzer_t *ctx, stock_analyzer_result_t *out)
{
    if (!ctx || !out || !ctx->initialized || !ctx->has_last_result)
        return STOCK_ANALYZER_ERR_ARGUMENT;
    *out = ctx->last_result;
    return STOCK_ANALYZER_OK;
}

stock_analyzer_status_t stock_analyzer_reset_session(stock_analyzer_t *ctx)
{
    if (!ctx || !ctx->initialized) return STOCK_ANALYZER_ERR_ARGUMENT;
    ctx->session_turnover = 0.0;
    ctx->session_volume = 0.0;
    ctx->has_partial_bar = 0u; /* caller must finalize a needed partial first */
    return STOCK_ANALYZER_OK;
}

stock_analyzer_status_t stock_analyzer_reset_all(stock_analyzer_t *ctx)
{
    stock_analyzer_config_t cfg;
    if (!ctx || !ctx->initialized) return STOCK_ANALYZER_ERR_ARGUMENT;
    cfg = ctx->cfg;
    return stock_analyzer_init(ctx, &cfg);
}
