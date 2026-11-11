#include "stock_monitor_ui.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "oled_canvas.h"
#include "stock_decision.h"
#include "stock_monitor_visual.h"
#include "time_service.h"

#define UI_PRICE_TOP 10
#define UI_PRICE_BAND_HEIGHT 18
#define UI_CHANGE_TOP 32
#define UI_CHANGE_BAND_HEIGHT 13
#define UI_DETAIL_RULE_Y 46
#define UI_DETAIL_ROW_Y 48
#define UI_BOTTOM_ROW_Y 56
#define UI_PAGE_DOT_Y 3
#define UI_RIGHT_MARGIN 2
#define UI_DECISION_LABEL_X 0
#define UI_DECISION_VALUE_X 32
#define UI_DECISION_RISK_DIVIDER_X 104

static int font_height(oled_canvas_font_t font)
{
    switch (font) {
    case OLED_CANVAS_FONT_DISPLAY:
        return 18;
    case OLED_CANVAS_FONT_WIDE:
    case OLED_CANVAS_FONT_VALUE:
        return 13;
    case OLED_CANVAS_FONT_SMALL:
    default:
        return 7;
    }
}

static int font_top_for_band(oled_canvas_font_t font, int band_top,
                             int band_height)
{
    int offset = (band_height - font_height(font)) / 2;

    return offset > 0 ? band_top + offset : band_top;
}

static int text_width_font(const char *text, oled_canvas_font_t font)
{
    if (text == NULL || text[0] == '\0') {
        return 0;
    }
    oled_canvas_set_font(font);
    return oled_canvas_text_width(text);
}

static bool text_fits(const char *text, oled_canvas_font_t font,
                      int max_width)
{
    return text_width_font(text, font) <= max_width;
}

static oled_canvas_font_t fit_font(const char *text,
                                   oled_canvas_font_t preferred,
                                   int max_width)
{
    if (text_fits(text, preferred, max_width)) {
        return preferred;
    }
    if (preferred == OLED_CANVAS_FONT_DISPLAY &&
        text_fits(text, OLED_CANVAS_FONT_WIDE, max_width)) {
        return OLED_CANVAS_FONT_WIDE;
    }
    if (preferred != OLED_CANVAS_FONT_SMALL &&
        text_fits(text, OLED_CANVAS_FONT_VALUE, max_width)) {
        return OLED_CANVAS_FONT_VALUE;
    }
    return OLED_CANVAS_FONT_SMALL;
}

static void draw_text_font(int x, int y, const char *text,
                           oled_canvas_font_t font, bool color)
{
    oled_canvas_set_font(font);
    oled_canvas_draw_text(x, y, text, color);
}

static void draw_right_font(int y, const char *text,
                            oled_canvas_font_t font, bool color)
{
    int x = OLED_CANVAS_WIDTH - UI_RIGHT_MARGIN -
            text_width_font(text, font);

    if (x < 0) {
        x = 0;
    }
    draw_text_font(x, y, text, font, color);
}

static void draw_centered_font(int y, const char *text,
                               oled_canvas_font_t font, bool color)
{
    int x = (OLED_CANVAS_WIDTH - text_width_font(text, font)) / 2;

    if (x < 0) {
        x = 0;
    }
    draw_text_font(x, y, text, font, color);
}

static void draw_line(int x0, int y0, int x1, int y1)
{
    int dx = x1 >= x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 >= y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    while (true) {
        int error2 = 2 * error;

        oled_canvas_fill_rect(x0, y0, 1, 1, true);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        if (error2 >= dy) {
            error += dy;
            x0 += sx;
        }
        if (error2 <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

static void draw_sparkline(const stock_price_history_t *history,
                           int x, int y, int width, int height)
{
    stock_sparkline_point_t points[STOCK_PRICE_HISTORY_MAX];
    size_t count = stock_monitor_sparkline_points(
        history, x, y, width, height, points,
        sizeof(points) / sizeof(points[0]));

    if (count == 0u) {
        return;
    }
    for (size_t index = 1u; index < count; index++) {
        draw_line(points[index - 1u].x, points[index - 1u].y,
                  points[index].x, points[index].y);
    }
    oled_canvas_fill_rect(points[count - 1u].x,
                          points[count - 1u].y, 2, 2, true);
}

static void format_volume(double volume_lots, char *buffer, size_t size)
{
    if (!isfinite(volume_lots) || volume_lots < 0.0) {
        (void)snprintf(buffer, size, "--");
    } else if (volume_lots >= 1000000.0) {
        (void)snprintf(buffer, size, "%.1fM", volume_lots / 1000000.0);
    } else if (volume_lots >= 1000.0) {
        (void)snprintf(buffer, size, "%.1fK", volume_lots / 1000.0);
    } else {
        (void)snprintf(buffer, size, "%.0f", volume_lots);
    }
}

static void format_quote_price(double value, char *buffer, size_t size)
{
    if (!isfinite(value) || value <= 0.0) {
        (void)snprintf(buffer, size, "--");
    } else {
        (void)snprintf(buffer, size, "%.2f", value);
    }
}

static void format_analysis_price(double value, char *buffer, size_t size)
{
    if (!isfinite(value) || value <= 0.0) {
        (void)snprintf(buffer, size, "--");
    } else if (value >= 1000000000.0) {
        (void)snprintf(buffer, size, "%.2fB", value / 1000000000.0);
    } else if (value >= 100000000.0) {
        (void)snprintf(buffer, size, "%.0f", value);
    } else if (value >= 10000000.0) {
        (void)snprintf(buffer, size, "%.1f", value);
    } else {
        (void)snprintf(buffer, size, "%.2f", value);
    }
}

static void format_decision_price(double value, char *buffer, size_t size)
{
    if (!isfinite(value) || value <= 0.0) {
        (void)snprintf(buffer, size, "--");
    } else if (value >= 1000000000000.0) {
        (void)snprintf(buffer, size, "%.2fT", value / 1000000000000.0);
    } else if (value >= 1000000000.0) {
        (void)snprintf(buffer, size, "%.2fB", value / 1000000000.0);
    } else if (value >= 1000000.0) {
        (void)snprintf(buffer, size, "%.2fM", value / 1000000.0);
    } else {
        (void)snprintf(buffer, size, "%.2f", value);
    }
}

static void draw_page_indicator(stock_monitor_view_t view)
{
    static const int centers[] = {112, 116, 120, 124};

    for (int page = 0; page < 4; page++) {
        if ((int)view == page) {
            oled_canvas_fill_rect(centers[page] - 1,
                                  UI_PAGE_DOT_Y - 1, 3, 3, true);
        } else {
            oled_canvas_fill_rect(centers[page], UI_PAGE_DOT_Y, 1, 1,
                                  true);
        }
    }
}

static void draw_header(const char *symbol, stock_monitor_view_t view,
                        bool show_clock)
{
    char code[STOCK_SYMBOL_MAX_LEN + 1u];
    char clock[16];
    time_service_snapshot_t time_snapshot;
    int code_width;
    int clock_width;
    int left;
    int right;
    int x;

    stock_monitor_format_symbol(symbol, code, sizeof(code));
    draw_text_font(0, 0, code, OLED_CANVAS_FONT_SMALL, true);
    if (show_clock) {
        time_service_get_snapshot(&time_snapshot);
        if (time_snapshot.valid) {
            stock_monitor_format_clock_weekday_from_unix(
                time_snapshot.unix_time, time_snapshot.utc_offset_s,
                clock, sizeof(clock));
        } else {
            stock_monitor_format_clock_weekday(-1, 0, 0, clock,
                                                sizeof(clock));
        }
        code_width = text_width_font(code, OLED_CANVAS_FONT_SMALL);
        clock_width = text_width_font(clock, OLED_CANVAS_FONT_SMALL);
        left = code_width + 6;
        right = OLED_CANVAS_WIDTH - 15;
        x = left + (right - left - clock_width) / 2;
        if (x < left) {
            x = left;
        }
        draw_text_font(x, 0, clock, OLED_CANVAS_FONT_SMALL, true);
    }
    draw_page_indicator(view);
}

static void draw_trend_mark(int x, int y, stock_trend_t trend)
{
    switch (trend) {
    case STOCK_TREND_UP:
        oled_canvas_fill_rect(x + 7, y, 2, 16, true);
        oled_canvas_fill_rect(x + 5, y + 2, 6, 2, true);
        oled_canvas_fill_rect(x + 3, y + 4, 10, 2, true);
        oled_canvas_fill_rect(x + 1, y + 6, 14, 2, true);
        break;
    case STOCK_TREND_DOWN:
        oled_canvas_fill_rect(x + 7, y, 2, 16, true);
        oled_canvas_fill_rect(x + 1, y + 8, 14, 2, true);
        oled_canvas_fill_rect(x + 3, y + 10, 10, 2, true);
        oled_canvas_fill_rect(x + 5, y + 12, 6, 2, true);
        break;
    case STOCK_TREND_SIDEWAYS:
        oled_canvas_fill_rect(x + 1, y + 7, 14, 2, true);
        oled_canvas_fill_rect(x + 9, y + 5, 2, 6, true);
        oled_canvas_fill_rect(x + 7, y + 6, 6, 4, true);
        break;
    case STOCK_TREND_UNKNOWN:
    default:
        oled_canvas_fill_rect(x + 6, y + 6, 4, 4, true);
        break;
    }
}

static void render_empty(void)
{
    oled_canvas_clear();
    draw_centered_font(28, "NO STOCKS", OLED_CANVAS_FONT_SMALL, true);
}

static void render_waiting(const stock_quote_t *quote,
                           stock_monitor_view_t view)
{
    oled_canvas_clear();
    draw_header(quote->symbol, view, view == STOCK_MONITOR_VIEW_QUOTE);
    draw_text_font(0, UI_PRICE_TOP, "--.--",
                   OLED_CANVAS_FONT_DISPLAY, true);
    draw_text_font(0, UI_CHANGE_TOP, "--.--%",
                   OLED_CANVAS_FONT_WIDE, true);
    draw_right_font(UI_CHANGE_TOP, "--.--", OLED_CANVAS_FONT_WIDE, true);
    oled_canvas_fill_rect(0, UI_DETAIL_RULE_Y, OLED_CANVAS_WIDTH, 1, true);
    draw_text_font(0, UI_DETAIL_ROW_Y, "H --", OLED_CANVAS_FONT_SMALL,
                   true);
    draw_right_font(UI_DETAIL_ROW_Y, "L --", OLED_CANVAS_FONT_SMALL, true);
    draw_text_font(0, UI_BOTTOM_ROW_Y, "VOL --", OLED_CANVAS_FONT_SMALL,
                   true);
    draw_right_font(UI_BOTTOM_ROW_Y, "AMT --", OLED_CANVAS_FONT_SMALL,
                    true);
}

static void render_quote(const stock_quote_t *quote,
                         const stock_price_history_t *history)
{
    char price[24];
    char change[24];
    char change_abs[24];
    char high[16];
    char low[16];
    char volume[16];
    char amount[20];
    char line[24];
    oled_canvas_font_t price_font;
    oled_canvas_font_t change_font;
    oled_canvas_font_t change_abs_font;
    int price_width;
    int chart_x;
    int chart_width;

    if (!quote->valid) {
        render_waiting(quote, STOCK_MONITOR_VIEW_QUOTE);
        return;
    }

    oled_canvas_clear();
    draw_header(quote->symbol, STOCK_MONITOR_VIEW_QUOTE, true);

    format_quote_price(quote->price, price, sizeof(price));
    price_font = fit_font(price, OLED_CANVAS_FONT_DISPLAY,
                          OLED_CANVAS_WIDTH - 4);
    price_width = text_width_font(price, price_font);
    draw_text_font(
        0, font_top_for_band(price_font, UI_PRICE_TOP,
                             UI_PRICE_BAND_HEIGHT),
        price, price_font, true);

    chart_x = price_width + 7;
    chart_width = OLED_CANVAS_WIDTH - chart_x;
    if (chart_width >= 16) {
        draw_sparkline(history, chart_x, UI_PRICE_TOP, chart_width,
                       UI_PRICE_BAND_HEIGHT);
    }

    (void)snprintf(change, sizeof(change), "%+.2f%%",
                   quote->change_percent);
    change_font = fit_font(change, OLED_CANVAS_FONT_WIDE, 72);
    draw_text_font(
        0, font_top_for_band(change_font, UI_CHANGE_TOP,
                             UI_CHANGE_BAND_HEIGHT),
        change, change_font, true);

    (void)snprintf(change_abs, sizeof(change_abs), "%+.2f",
                   quote->change);
    change_abs_font = fit_font(change_abs, OLED_CANVAS_FONT_WIDE, 48);
    draw_right_font(
        font_top_for_band(change_abs_font, UI_CHANGE_TOP,
                          UI_CHANGE_BAND_HEIGHT),
        change_abs, change_abs_font, true);

    oled_canvas_fill_rect(0, UI_DETAIL_RULE_Y, OLED_CANVAS_WIDTH, 1,
                          true);
    format_quote_price(quote->high, high, sizeof(high));
    format_quote_price(quote->low, low, sizeof(low));
    (void)snprintf(line, sizeof(line), "H %s", high);
    draw_text_font(0, UI_DETAIL_ROW_Y, line, OLED_CANVAS_FONT_SMALL,
                   true);
    (void)snprintf(line, sizeof(line), "L %s", low);
    draw_right_font(UI_DETAIL_ROW_Y, line, OLED_CANVAS_FONT_SMALL, true);

    format_volume(quote->volume_lots, volume, sizeof(volume));
    (void)snprintf(line, sizeof(line), "VOL %s", volume);
    draw_text_font(0, UI_BOTTOM_ROW_Y, line, OLED_CANVAS_FONT_SMALL,
                   true);
    stock_monitor_format_turnover(quote->turnover_cny, amount,
                                  sizeof(amount));
    draw_right_font(UI_BOTTOM_ROW_Y, amount, OLED_CANVAS_FONT_SMALL,
                    true);
}

static void render_indicators(const stock_quote_t *quote,
                              const stock_analysis_snapshot_t *analysis)
{
    stock_analyzer_result_t display =
        stock_decision_effective_analysis(analysis);
    char rsi[12] = "--";
    char macd[12] = "--";
    char ema[16] = "--";
    char volume[12] = "--";
    oled_canvas_font_t rsi_font;
    oled_canvas_font_t macd_font;
    oled_canvas_font_t ema_font;
    oled_canvas_font_t volume_font;
    int rsi_top;
    int macd_top;

    oled_canvas_clear();
    draw_header(quote->symbol, STOCK_MONITOR_VIEW_INDICATORS, false);
    if (analysis->valid) {
        if ((display.ready & STOCK_READY_RSI) != 0u) {
            (void)snprintf(rsi, sizeof(rsi), "%.0f", display.rsi);
        }
        if ((display.ready & STOCK_READY_MACD) != 0u) {
            (void)snprintf(macd, sizeof(macd), "%+.2f",
                           display.macd_histogram);
        }
        if ((display.ready & STOCK_READY_EMA) != 0u &&
            fabs(display.ema_slow) > 0.0001) {
            (void)snprintf(
                ema, sizeof(ema), "%+.2f%%",
                (display.ema_fast - display.ema_slow) /
                    display.ema_slow * 100.0);
        }
        if ((display.ready & STOCK_READY_VOLUME) != 0u) {
            (void)snprintf(volume, sizeof(volume), "%.1fX",
                           display.volume_ratio);
        }
    }

    draw_text_font(0, 10, "RSI", OLED_CANVAS_FONT_SMALL, true);
    draw_text_font(68, 10, "MACD", OLED_CANVAS_FONT_SMALL, true);
    oled_canvas_fill_rect(63, 10, 1, 27, true);

    rsi_font = fit_font(rsi, OLED_CANVAS_FONT_DISPLAY, 60);
    macd_font = fit_font(macd, OLED_CANVAS_FONT_DISPLAY, 57);
    rsi_top = font_top_for_band(rsi_font, 18, UI_PRICE_BAND_HEIGHT);
    macd_top = font_top_for_band(macd_font, 18, UI_PRICE_BAND_HEIGHT);
    draw_text_font(0, rsi_top, rsi, rsi_font, true);
    draw_text_font(68, macd_top, macd, macd_font, true);

    oled_canvas_fill_rect(0, 40, OLED_CANVAS_WIDTH, 1, true);
    draw_text_font(0, 42, "EMA", OLED_CANVAS_FONT_SMALL, true);
    draw_right_font(42, "VOL", OLED_CANVAS_FONT_SMALL, true);

    ema_font = fit_font(ema, OLED_CANVAS_FONT_VALUE, 60);
    volume_font = fit_font(volume, OLED_CANVAS_FONT_VALUE, 60);
    draw_text_font(
        0, font_top_for_band(ema_font, 49, UI_CHANGE_BAND_HEIGHT), ema,
        ema_font, true);
    draw_right_font(
        font_top_for_band(volume_font, 49, UI_CHANGE_BAND_HEIGHT),
        volume, volume_font, true);
}

static void render_analysis(const stock_quote_t *quote,
                            const stock_analysis_snapshot_t *analysis)
{
    stock_analyzer_result_t display =
        stock_decision_effective_analysis(analysis);
    char line[40];
    char open[16] = "--";
    char previous[16] = "--";
    char vwap[16] = "--";
    char vwap_change[16] = "--";
    char boll_lower[16] = "--";
    char boll_upper[16] = "--";
    char score[12] = "--";
    stock_trend_t trend = STOCK_TREND_UNKNOWN;
    oled_canvas_font_t score_font;
    oled_canvas_font_t vwap_font;
    int score_width;
    int score_label_x;

    oled_canvas_clear();
    draw_header(quote->symbol, STOCK_MONITOR_VIEW_ANALYSIS, false);
    format_analysis_price(quote->open, open, sizeof(open));
    format_analysis_price(quote->previous_close, previous,
                          sizeof(previous));

    if (analysis->valid) {
        trend = display.trend;
        (void)snprintf(score, sizeof(score), "%+d",
                       (int)display.score);
        if ((display.ready & STOCK_READY_VWAP) != 0u &&
            fabs(display.vwap) > 0.0001) {
            format_analysis_price(display.vwap, vwap,
                                  sizeof(vwap));
            (void)snprintf(
                vwap_change, sizeof(vwap_change), "%+.2f%%",
                (quote->price - display.vwap) /
                    display.vwap * 100.0);
        }
        if ((display.ready & STOCK_READY_BOLL) != 0u) {
            format_analysis_price(display.boll_lower, boll_lower,
                                  sizeof(boll_lower));
            format_analysis_price(display.boll_upper, boll_upper,
                                  sizeof(boll_upper));
        }
    }

    score_font = fit_font(score, OLED_CANVAS_FONT_DISPLAY, 36);
    score_width = text_width_font(score, score_font);
    draw_text_font(
        0, font_top_for_band(score_font, UI_PRICE_TOP,
                             UI_PRICE_BAND_HEIGHT),
        score, score_font, true);
    score_label_x = score_width + 6;
    if (score_label_x > 76) {
        score_label_x = 76;
    }
    draw_text_font(score_label_x, 12, "SCORE", OLED_CANVAS_FONT_SMALL,
                   true);
    draw_trend_mark(112, 10, trend);

    oled_canvas_fill_rect(0, 34, OLED_CANVAS_WIDTH, 1, true);
    (void)snprintf(line, sizeof(line), "O %s", open);
    draw_text_font(0, 37, line, OLED_CANVAS_FONT_SMALL, true);
    (void)snprintf(line, sizeof(line), "P %s", previous);
    draw_right_font(37, line, OLED_CANVAS_FONT_SMALL, true);

    draw_text_font(0, 46, "VWAP", OLED_CANVAS_FONT_SMALL, true);
    vwap_font = fit_font(vwap, OLED_CANVAS_FONT_VALUE, 60);
    draw_text_font(
        27, font_top_for_band(vwap_font, 43, UI_CHANGE_BAND_HEIGHT),
        vwap, vwap_font, true);
    draw_right_font(47, vwap_change, OLED_CANVAS_FONT_SMALL, true);

    (void)snprintf(line, sizeof(line), "BOLL %s %s", boll_lower,
                   boll_upper);
    draw_text_font(0, UI_BOTTOM_ROW_Y, line, OLED_CANVAS_FONT_SMALL,
                   true);
}

static const char *decision_label(stock_decision_state_t state)
{
    switch (state) {
    case STOCK_DECISION_READY:
        return "READY";
    case STOCK_DECISION_AVOID:
        return "AVOID";
    case STOCK_DECISION_WAIT:
        return "WAIT";
    case STOCK_DECISION_UNAVAILABLE:
    default:
        return "NO DATA";
    }
}

static void render_decision(const stock_quote_t *quote,
                            const stock_analysis_snapshot_t *analysis)
{
    stock_decision_t decision;
    char current[16] = "--";
    char entry_low[16] = "--";
    char entry_high[16] = "--";
    char entry[36];
    char stop[16] = "--";
    char target[16] = "--";
    char risk[16] = "--";
    char reward[16] = "--";
    const char *label;
    oled_canvas_font_t status_font;
    oled_canvas_font_t current_font;

    stock_decision_evaluate(quote, analysis, &decision);
    label = decision_label(decision.state);
    format_decision_price(quote->price, current, sizeof(current));
    if (decision.state != STOCK_DECISION_UNAVAILABLE &&
        decision.state != STOCK_DECISION_AVOID) {
        format_decision_price(decision.entry_low, entry_low,
                              sizeof(entry_low));
        format_decision_price(decision.entry_high, entry_high,
                              sizeof(entry_high));
        format_decision_price(decision.invalidation, stop, sizeof(stop));
        format_decision_price(decision.target, target, sizeof(target));
        if (decision.risk_percent > 0.0) {
            (void)snprintf(risk, sizeof(risk), "%.1f%%",
                           decision.risk_percent);
        }
        if (decision.reward_risk > 0.0) {
            (void)snprintf(reward, sizeof(reward), "%.1fR",
                           decision.reward_risk);
        }
    }
    if (strcmp(entry_low, "--") != 0 && strcmp(entry_high, "--") != 0) {
        (void)snprintf(entry, sizeof(entry), "%s-%s", entry_low,
                       entry_high);
    } else {
        (void)snprintf(entry, sizeof(entry), "--");
    }

    oled_canvas_clear();
    draw_header(quote->symbol, STOCK_MONITOR_VIEW_DECISION, false);
    status_font = fit_font(label, OLED_CANVAS_FONT_DISPLAY, 68);
    current_font = fit_font(current, OLED_CANVAS_FONT_VALUE, 48);
    draw_text_font(
        0, font_top_for_band(status_font, UI_PRICE_TOP,
                             UI_PRICE_BAND_HEIGHT),
        label, status_font, true);
    draw_right_font(
        font_top_for_band(current_font, UI_PRICE_TOP, UI_PRICE_BAND_HEIGHT),
        current, current_font, true);

    oled_canvas_fill_rect(0, 31, OLED_CANVAS_WIDTH, 1, true);
    draw_text_font(UI_DECISION_LABEL_X, 34, "ENTRY",
                   OLED_CANVAS_FONT_SMALL, true);
    draw_text_font(UI_DECISION_VALUE_X, 34, entry,
                   OLED_CANVAS_FONT_SMALL, true);

    oled_canvas_fill_rect(UI_DECISION_RISK_DIVIDER_X, 42, 1, 18, true);
    draw_text_font(UI_DECISION_LABEL_X, 43, "STOP",
                   OLED_CANVAS_FONT_SMALL, true);
    draw_text_font(UI_DECISION_VALUE_X, 43, stop,
                   OLED_CANVAS_FONT_SMALL, true);
    draw_right_font(43, risk, OLED_CANVAS_FONT_SMALL, true);

    draw_text_font(UI_DECISION_LABEL_X, 52, "TGT",
                   OLED_CANVAS_FONT_SMALL, true);
    draw_text_font(UI_DECISION_VALUE_X, 52, target,
                   OLED_CANVAS_FONT_SMALL, true);
    draw_right_font(52, reward, OLED_CANVAS_FONT_SMALL, true);
}

void stock_monitor_ui_render(const stock_service_snapshot_t *snapshot,
                             const stock_monitor_state_t *state)
{
    const stock_quote_t *quote = NULL;

    if (snapshot == NULL || state == NULL) {
        return;
    }
    if (state->symbol_count > 0u &&
        state->selected_index < snapshot->symbol_count) {
        quote = &snapshot->quotes[state->selected_index];
    }
    if (quote == NULL || state->data_state == STOCK_MONITOR_DATA_EMPTY) {
        render_empty();
        return;
    }

    switch (state->view) {
    case STOCK_MONITOR_VIEW_INDICATORS:
        render_indicators(quote, &snapshot->analysis[state->selected_index]);
        break;
    case STOCK_MONITOR_VIEW_ANALYSIS:
        render_analysis(quote, &snapshot->analysis[state->selected_index]);
        break;
    case STOCK_MONITOR_VIEW_DECISION:
        render_decision(quote, &snapshot->analysis[state->selected_index]);
        break;
    case STOCK_MONITOR_VIEW_QUOTE:
    default:
        render_quote(
            quote, &snapshot->intraday_history[state->selected_index]);
        break;
    }
}
