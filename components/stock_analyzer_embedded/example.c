/* Compile: gcc -std=c99 -O2 example.c stock_analyzer.c -lm -o example */
#include "stock_analyzer.h"
#include <stdio.h>

/* In embedded firmware use a static/global instance to save task stack. */
static stock_analyzer_t g_a_stock;
static stock_analyzer_t g_b_stock;

static const char *trend_name(stock_trend_t trend)
{
    switch (trend) {
    case STOCK_TREND_UP:       return "UP";
    case STOCK_TREND_DOWN:     return "DOWN";
    case STOCK_TREND_SIDEWAYS: return "SIDEWAYS";
    default:                   return "UNKNOWN";
    }
}

static void display(const char *symbol, const stock_analyzer_result_t *r)
{
    printf("%s | %s | close %.2f | trend %s | score %+d | flags 0x%04lx",
           symbol, r->is_closed ? "CLOSED" : "LIVE",
           (double)r->close, trend_name(r->trend), (int)r->score,
           (unsigned long)r->signals);
    if (r->ready & STOCK_READY_RSI) printf(" | RSI %.1f", (double)r->rsi);
    if (r->ready & STOCK_READY_VWAP) printf(" | VWAP %.2f", (double)r->vwap);
    if (r->ready & STOCK_READY_MACD)
        printf(" | MACD_hist %.4f", (double)r->macd_histogram);
    puts("");
}

int main(void)
{
    stock_analyzer_config_t cfg;
    stock_analyzer_result_t result;
    stock_analyzer_result_t closed;
    stock_bar_t bar;
    stock_tick_t tick;
    stock_analyzer_status_t st;
    unsigned i;
    float price = 10.0f;

    stock_analyzer_default_config(&cfg);
    if (stock_analyzer_init(&g_a_stock, &cfg) < 0 ||
        stock_analyzer_init(&g_b_stock, &cfg) < 0) return 1;

    /* Approach A: finalized OHLCV minutes from your data-source module. */
    for (i = 0; i < 50u; ++i) {
        price += i < 35u ? 0.03f : -0.04f;
        bar.timestamp_s = 1794274200u + i * 60u; /* illustrative Unix seconds */
        bar.open = price - 0.01f;
        bar.high = price + 0.05f;
        bar.low = price - 0.05f;
        bar.close = price;
        bar.volume = 1000.0f + (float)(i % 6u) * 100.0f;
        bar.turnover = bar.close * bar.volume; /* same units as volume */
        st = stock_analyzer_push_bar(&g_a_stock, &bar, &result);
        if (st < 0) return 2;
    }
    display("000001", &result);

    /* Approach B: live ticks; volume_delta is INCREMENTAL, not day total. */
    tick.timestamp_s = 1794274200u;
    tick.price = 20.00f;
    tick.volume_delta = 100.0f;
    st = stock_analyzer_feed_tick(&g_b_stock, &tick, &result, &closed);
    if (st < 0) return 3;
    display("600519", &result); /* incomplete bar */

    tick.timestamp_s += 5u;
    tick.price = 20.05f;
    tick.volume_delta = 200.0f;
    st = stock_analyzer_feed_tick(&g_b_stock, &tick, &result, &closed);
    if (st < 0) return 4;

    tick.timestamp_s += 60u;
    tick.price = 20.10f;
    tick.volume_delta = 150.0f;
    st = stock_analyzer_feed_tick(&g_b_stock, &tick, &result, &closed);
    if (st == STOCK_ANALYZER_BAR_COMPLETED)
        display("600519", &closed); /* previous minute finalized */
    display("600519", &result);     /* current minute still live */

    /* Flush at market close or the end of a trading session. */
    st = stock_analyzer_flush_tick(&g_b_stock, &closed);
    if (st == STOCK_ANALYZER_BAR_COMPLETED) display("600519", &closed);

    printf("RAM per analyzer: %lu bytes\n", (unsigned long)sizeof(stock_analyzer_t));
    return 0;
}
