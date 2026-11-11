#include "stock_monitor_ui.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "oled_canvas.h"

static bool pixel_on(int x, int y)
{
    const uint8_t *framebuffer = oled_canvas_framebuffer();
    uint16_t index = (uint16_t)((y / 8) * OLED_CANVAS_WIDTH + x);

    return (framebuffer[index] & (uint8_t)(1u << (y & 7))) != 0u;
}

static bool row_has_ink(int y)
{
    for (int x = 0; x < OLED_CANVAS_WIDTH; x++) {
        if (pixel_on(x, y)) {
            return true;
        }
    }
    return false;
}

static bool column_has_ink(int x, int first_y, int last_y)
{
    for (int y = first_y; y <= last_y; y++) {
        if (pixel_on(x, y)) {
            return true;
        }
    }
    return false;
}

static void assert_page_frame(bool expect_clock)
{
    bool clock_ink = false;

    assert(oled_canvas_framebuffer_size() ==
           OLED_CANVAS_WIDTH * OLED_CANVAS_PAGE_COUNT);
    assert(!row_has_ink(7));
    assert(!row_has_ink(8));
    for (int y = 0; y <= 6; y++) {
        for (int x = 40; x <= 110; x++) {
            if (pixel_on(x, y)) {
                clock_ink = true;
            }
        }
    }
    assert(clock_ink == expect_clock);
    assert(!row_has_ink(63));
}

static void assert_decision_frame(void)
{
    assert_page_frame(false);
    assert(!column_has_ink(OLED_CANVAS_WIDTH - 1, 9, 30));
    assert(!column_has_ink(OLED_CANVAS_WIDTH - 1, 32,
                           OLED_CANVAS_HEIGHT - 2));
}

static void write_pbm(const char *path)
{
    FILE *file = fopen(path, "wb");

    assert(file != NULL);
    assert(fprintf(file, "P4\n%d %d\n", OLED_CANVAS_WIDTH,
                   OLED_CANVAS_HEIGHT) > 0);
    for (int y = 0; y < OLED_CANVAS_HEIGHT; y++) {
        uint8_t row[OLED_CANVAS_WIDTH / 8] = {0};

        for (int x = 0; x < OLED_CANVAS_WIDTH; x++) {
            if (pixel_on(x, y)) {
                row[x / 8] |= (uint8_t)(0x80u >> (x & 7));
            }
        }
        assert(fwrite(row, 1u, sizeof(row), file) == sizeof(row));
    }
    assert(fclose(file) == 0);
}

static void fill_normal_snapshot(stock_service_snapshot_t *snapshot)
{
    stock_quote_t *quote = &snapshot->quotes[0];
    stock_analysis_snapshot_t *analysis = &snapshot->analysis[0];
    stock_price_history_t *history = &snapshot->intraday_history[0];

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->symbol_count = 1u;
    snapshot->analysis_ready_count = 1u;
    (void)snprintf(quote->symbol, sizeof(quote->symbol), "sh600519");
    (void)snprintf(quote->code, sizeof(quote->code), "600519");
    quote->valid = true;
    quote->price = 1728.60;
    quote->previous_close = 1702.30;
    quote->open = 1708.00;
    quote->high = 1736.50;
    quote->low = 1698.20;
    quote->change = 26.30;
    quote->change_percent = 1.54;
    quote->volume_lots = 34567.0;
    quote->turnover_cny = 5980000000.0;

    history->count = 32u;
    for (uint8_t index = 0u; index < history->count; index++) {
        float position = (float)index;

        history->prices[index] =
            1704.0f + position * 0.62f + sinf(position * 0.55f) * 3.2f;
    }

    analysis->supported = true;
    analysis->valid = true;
    analysis->live.ready = STOCK_READY_EMA | STOCK_READY_MACD |
                           STOCK_READY_RSI | STOCK_READY_VWAP |
                           STOCK_READY_BOLL | STOCK_READY_VOLUME;
    analysis->live.trend = STOCK_TREND_UP;
    analysis->live.score = 42;
    analysis->live.ema_fast = 1712.4f;
    analysis->live.ema_slow = 1698.6f;
    analysis->live.macd_histogram = 0.84f;
    analysis->live.rsi = 63.0f;
    analysis->live.vwap = 1716.2f;
    analysis->live.boll_lower = 1668.0f;
    analysis->live.boll_upper = 1742.5f;
    analysis->live.volume_ratio = 1.6f;
}

static void render_normal_pages(void)
{
    stock_service_snapshot_t snapshot;
    stock_monitor_state_t state;

    fill_normal_snapshot(&snapshot);
    stock_monitor_state_init(&state, 1u);
    state.data_state = STOCK_MONITOR_DATA_FRESH;

    state.view = STOCK_MONITOR_VIEW_QUOTE;
    stock_monitor_ui_render(&snapshot, &state);
    assert_page_frame(true);
    write_pbm("oled_quote_normal.pbm");

    state.view = STOCK_MONITOR_VIEW_INDICATORS;
    stock_monitor_ui_render(&snapshot, &state);
    assert_page_frame(false);
    write_pbm("oled_indicators_normal.pbm");

    state.view = STOCK_MONITOR_VIEW_ANALYSIS;
    stock_monitor_ui_render(&snapshot, &state);
    assert_page_frame(false);
    write_pbm("oled_analysis_normal.pbm");
}

static void render_decision_pages(void)
{
    stock_service_snapshot_t snapshot;
    stock_monitor_state_t state;

    fill_normal_snapshot(&snapshot);
    stock_monitor_state_init(&state, 1u);
    state.data_state = STOCK_MONITOR_DATA_FRESH;
    state.view = STOCK_MONITOR_VIEW_DECISION;

    snapshot.quotes[0].price = 1720.0;
    snapshot.analysis[0].live.boll_upper = 1780.0f;
    stock_monitor_ui_render(&snapshot, &state);
    assert_decision_frame();
    write_pbm("oled_decision_ready.pbm");

    snapshot.quotes[0].price = 1800.0;
    snapshot.quotes[0].high = 1810.0;
    snapshot.analysis[0].live.rsi = 76.0f;
    stock_monitor_ui_render(&snapshot, &state);
    assert_decision_frame();
    write_pbm("oled_decision_wait.pbm");

    snapshot.quotes[0].price = 1600.0;
    snapshot.analysis[0].live.score = -80;
    snapshot.analysis[0].live.trend = STOCK_TREND_DOWN;
    snapshot.analysis[0].live.ema_fast = 1610.0f;
    snapshot.analysis[0].live.ema_slow = 1650.0f;
    snapshot.analysis[0].live.macd_histogram = -1.2f;
    snapshot.analysis[0].live.vwap = 1680.0f;
    stock_monitor_ui_render(&snapshot, &state);
    assert_decision_frame();
    write_pbm("oled_decision_avoid.pbm");

    snapshot.analysis[0].valid = false;
    stock_monitor_ui_render(&snapshot, &state);
    assert_decision_frame();
    write_pbm("oled_decision_no_data.pbm");

    fill_normal_snapshot(&snapshot);
    snapshot.quotes[0].price *= 100000.0;
    snapshot.quotes[0].high *= 100000.0;
    snapshot.quotes[0].low *= 100000.0;
    snapshot.analysis[0].live.ema_fast *= 100000.0f;
    snapshot.analysis[0].live.ema_slow *= 100000.0f;
    snapshot.analysis[0].live.vwap *= 100000.0f;
    snapshot.analysis[0].live.boll_middle *= 100000.0f;
    snapshot.analysis[0].live.boll_lower *= 100000.0f;
    snapshot.analysis[0].live.boll_upper *= 100000.0f;
    snapshot.analysis[0].live.macd_histogram *= 100000.0f;
    stock_monitor_ui_render(&snapshot, &state);
    assert_decision_frame();
    write_pbm("oled_decision_long_price.pbm");
}

static void render_extreme_values(void)
{
    stock_service_snapshot_t snapshot;
    stock_monitor_state_t state;

    fill_normal_snapshot(&snapshot);
    stock_monitor_state_init(&state, 1u);
    state.data_state = STOCK_MONITOR_DATA_FRESH;
    state.view = STOCK_MONITOR_VIEW_QUOTE;
    snapshot.quotes[0].price = 1234567.89;
    snapshot.quotes[0].high = 1234568.90;
    snapshot.quotes[0].low = 1200000.10;
    snapshot.quotes[0].change_percent = 3.48;
    stock_monitor_ui_render(&snapshot, &state);
    assert_page_frame(true);
    write_pbm("oled_quote_long_price.pbm");

    snapshot.quotes[0].price = 12.34;
    snapshot.quotes[0].change = -0.46;
    snapshot.quotes[0].change_percent = -3.59;
    snapshot.quotes[0].volume_lots = 98765432.0;
    snapshot.quotes[0].turnover_cny = 1250000000.0;
    snapshot.analysis[0].valid = false;
    state.view = STOCK_MONITOR_VIEW_INDICATORS;
    stock_monitor_ui_render(&snapshot, &state);
    assert_page_frame(false);
    write_pbm("oled_indicators_missing.pbm");

    state.view = STOCK_MONITOR_VIEW_ANALYSIS;
    stock_monitor_ui_render(&snapshot, &state);
    assert_page_frame(false);
    write_pbm("oled_analysis_missing.pbm");
}

static void render_live_falls_back_to_closed(void)
{
    stock_service_snapshot_t snapshot;
    stock_monitor_state_t state;
    stock_analyzer_result_t complete;
    uint8_t expected[OLED_CANVAS_WIDTH * OLED_CANVAS_PAGE_COUNT];

    fill_normal_snapshot(&snapshot);
    stock_monitor_state_init(&state, 1u);
    state.data_state = STOCK_MONITOR_DATA_FRESH;
    state.view = STOCK_MONITOR_VIEW_INDICATORS;
    complete = snapshot.analysis[0].live;
    stock_monitor_ui_render(&snapshot, &state);
    memcpy(expected, oled_canvas_framebuffer(), sizeof(expected));

    snapshot.analysis[0].live.ready = STOCK_READY_EMA | STOCK_READY_MACD;
    snapshot.analysis[0].has_closed = true;
    snapshot.analysis[0].closed = complete;
    stock_monitor_ui_render(&snapshot, &state);
    assert(memcmp(expected, oled_canvas_framebuffer(), sizeof(expected)) == 0);

    state.view = STOCK_MONITOR_VIEW_ANALYSIS;
    snapshot.analysis[0].live = complete;
    stock_monitor_ui_render(&snapshot, &state);
    memcpy(expected, oled_canvas_framebuffer(), sizeof(expected));

    snapshot.analysis[0].live.ready = STOCK_READY_EMA | STOCK_READY_MACD |
                                      STOCK_READY_RSI | STOCK_READY_VOLUME;
    stock_monitor_ui_render(&snapshot, &state);
    assert(memcmp(expected, oled_canvas_framebuffer(), sizeof(expected)) == 0);
}

int main(void)
{
    render_normal_pages();
    render_decision_pages();
    render_extreme_values();
    render_live_falls_back_to_closed();
    puts("stock monitor UI tests passed");
    return 0;
}
