#ifndef STOCK_ANALYZER_H
#define STOCK_ANALYZER_H

/*
 * Embedded stock technical analysis engine, C99.
 * Copyright (c) 2026. MIT License; see README.md.
 *
 * No heap, filesystem, network, threads or RTOS dependencies.
 * One stock_analyzer_t instance per watched stock.
 * NOTE: This module analyzes market data; it does NOT fetch market data.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STOCK_ANALYZER_MAX_WINDOW 32u

/* Return values: >= 0 success, < 0 error. */
typedef enum {
    STOCK_ANALYZER_OK            =  0,
    STOCK_ANALYZER_BAR_COMPLETED =  1, /* feed_tick closed previous bar */
    STOCK_ANALYZER_ERR_ARGUMENT  = -1,
    STOCK_ANALYZER_ERR_CONFIG    = -2,
    STOCK_ANALYZER_ERR_TIME      = -3,
    STOCK_ANALYZER_ERR_MODE      = -4
} stock_analyzer_status_t;

typedef enum {
    STOCK_TREND_UNKNOWN  =  0,
    STOCK_TREND_UP       =  1,
    STOCK_TREND_DOWN     = -1,
    STOCK_TREND_SIDEWAYS =  2
} stock_trend_t;

/* Check the corresponding ready bit before using an indicator value. */
#define STOCK_READY_EMA       (1u << 0)
#define STOCK_READY_MACD      (1u << 1)
#define STOCK_READY_RSI       (1u << 2)
#define STOCK_READY_VWAP      (1u << 3)
#define STOCK_READY_BOLL      (1u << 4)
#define STOCK_READY_VOLUME    (1u << 5)
#define STOCK_READY_BREAKOUT  (1u << 6)

/* Observational signals, NOT instructions to buy or sell. */
#define STOCK_SIGNAL_EMA_BULL       (1u << 0)
#define STOCK_SIGNAL_EMA_BEAR       (1u << 1)
#define STOCK_SIGNAL_ABOVE_VWAP     (1u << 2)
#define STOCK_SIGNAL_BELOW_VWAP     (1u << 3)
#define STOCK_SIGNAL_VOLUME_SPIKE   (1u << 4)
#define STOCK_SIGNAL_BREAKOUT_UP    (1u << 5)
#define STOCK_SIGNAL_BREAKOUT_DOWN  (1u << 6)
#define STOCK_SIGNAL_RSI_HIGH       (1u << 7) /* RSI >= 70 */
#define STOCK_SIGNAL_RSI_LOW        (1u << 8) /* RSI <= 30 */
#define STOCK_SIGNAL_MACD_CROSS_UP  (1u << 9) /* histogram crossed 0 upward */
#define STOCK_SIGNAL_MACD_CROSS_DN  (1u << 10)
#define STOCK_SIGNAL_ABOVE_BOLL     (1u << 11)
#define STOCK_SIGNAL_BELOW_BOLL     (1u << 12)

typedef struct {
    uint8_t ema_fast_period;      /* default 5 */
    uint8_t ema_slow_period;      /* default 20 */
    uint8_t macd_fast_period;     /* default 12 */
    uint8_t macd_slow_period;     /* default 26 */
    uint8_t macd_signal_period;   /* default 9 */
    uint8_t rsi_period;           /* default 14 */
    uint8_t boll_period;          /* default 20 */
    uint8_t volume_period;        /* default 20, previous bars */
    uint8_t breakout_period;      /* default 20, previous bars */
    float boll_stddev;            /* default 2.0 */
    float volume_spike_ratio;     /* default 1.8 */
    uint16_t bar_seconds;         /* default 60 for one-minute bars */
} stock_analyzer_config_t;

/* Timestamp is Unix seconds at the START of the bar. Prices in same unit. */
typedef struct {
    uint32_t timestamp_s;
    float open;
    float high;
    float low;
    float close;
    float volume;      /* Incremental volume for this bar, preferably shares */
    float turnover;    /* Price * volume, 0 => approximate using (H+L+C)/3 */
} stock_bar_t;

/* Tick volume_delta MUST be incremental, not cumulative daily volume. */
typedef struct {
    uint32_t timestamp_s;
    float price;
    float volume_delta; /* >= 0, consistent with stock_bar_t.volume units */
} stock_tick_t;

typedef struct {
    uint32_t bar_start_s;
    uint32_t completed_bars;
    uint32_t ready;               /* STOCK_READY_* bit mask */
    uint32_t signals;             /* STOCK_SIGNAL_* bit mask */
    stock_trend_t trend;
    int16_t score;                /* heuristic [-100,100], NOT a probability */
    uint8_t is_closed;            /* 1=finalized bar, 0=provisional tick bar */
    float close;
    float ema_fast;
    float ema_slow;
    float macd;
    float macd_signal;
    float macd_histogram;
    float rsi;
    float vwap;
    float boll_middle;
    float boll_upper;
    float boll_lower;
    float volume_ratio;          /* volume / mean previous N bars */
} stock_analyzer_result_t;

/* Public state so it can be statically allocated in bare-metal firmware.
 * Do not modify fields directly; use the API below.
 */
typedef struct {
    stock_analyzer_config_t cfg;
    float close_history[STOCK_ANALYZER_MAX_WINDOW];
    float high_history[STOCK_ANALYZER_MAX_WINDOW];
    float low_history[STOCK_ANALYZER_MAX_WINDOW];
    float volume_history[STOCK_ANALYZER_MAX_WINDOW];
    uint8_t history_count;
    uint8_t history_next;
    uint8_t initialized;
    uint8_t mode;                  /* 0=unused, 1=bars, 2=ticks */
    uint32_t completed_bars;
    uint32_t last_bar_start_s;
    uint32_t last_tick_s;
    uint8_t has_previous_close;
    uint8_t has_previous_macd;
    uint8_t has_last_result;
    uint8_t has_partial_bar;
    float previous_close;
    float previous_macd_histogram;
    float ema_fast;
    float ema_slow;
    float macd_fast_ema;
    float macd_slow_ema;
    float macd_signal_ema;
    float rsi_avg_gain;
    float rsi_avg_loss;
    uint8_t rsi_seed_count;
    uint8_t macd_signal_count;
    double session_turnover;
    double session_volume;
    stock_bar_t partial_bar;
    stock_analyzer_result_t last_result;
} stock_analyzer_t;

/* Populate cfg with defaults; customize before calling init. */
void stock_analyzer_default_config(stock_analyzer_config_t *cfg);
stock_analyzer_status_t stock_analyzer_init(
    stock_analyzer_t *ctx, const stock_analyzer_config_t *cfg);

/* Feed one finalized OHLCV bar, such as a completed one-minute candle. */
stock_analyzer_status_t stock_analyzer_push_bar(
    stock_analyzer_t *ctx, const stock_bar_t *bar,
    stock_analyzer_result_t *out);

/* Feed individual ticks. On each call:
 *   live_out: analysis of the current INCOMPLETE bar (may be NULL);
 *   closed_out: previous finalized bar only if return == BAR_COMPLETED.
 * A minute with no ticks is NOT filled with fabricated data.
 * Call reset_session() at the start of each trading day for correct VWAP.
 */
stock_analyzer_status_t stock_analyzer_feed_tick(
    stock_analyzer_t *ctx, const stock_tick_t *tick,
    stock_analyzer_result_t *live_out,
    stock_analyzer_result_t *closed_out);

/* Close the pending tick bar at session end, e.g. at 11:30 or 15:00.
 * No-op (OK) if no partial bar exists; otherwise returns BAR_COMPLETED.
 */
stock_analyzer_status_t stock_analyzer_flush_tick(
    stock_analyzer_t *ctx, stock_analyzer_result_t *closed_out);

/* Get most recent finalized-bar result. */
stock_analyzer_status_t stock_analyzer_last_result(
    const stock_analyzer_t *ctx, stock_analyzer_result_t *out);

/* Keep indicator history but reset intraday VWAP and discard an incomplete
 * tick bar. Call once when a NEW trading day starts; not during lunch break.
 */
stock_analyzer_status_t stock_analyzer_reset_session(stock_analyzer_t *ctx);

/* Fully reset indicators and buffers while preserving configuration. */
stock_analyzer_status_t stock_analyzer_reset_all(stock_analyzer_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* STOCK_ANALYZER_H */
