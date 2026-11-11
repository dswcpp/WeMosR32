#include "stock_monitor_visual.h"

#include <assert.h>
#include <string.h>

static void format_turnover(void)
{
    char text[20];

    stock_monitor_format_turnover(0.0, text, sizeof(text));
    assert(strcmp(text, "AMT N/A") == 0);
    stock_monitor_format_turnover(12500.0, text, sizeof(text));
    assert(strcmp(text, "AMT 12.5K") == 0);
    stock_monitor_format_turnover(370310000.0, text, sizeof(text));
    assert(strcmp(text, "AMT 370.3M") == 0);
    stock_monitor_format_turnover(1250000000.0, text, sizeof(text));
    assert(strcmp(text, "AMT 1.25B") == 0);
}

static void format_symbol(void)
{
    char text[16];

    stock_monitor_format_symbol("sh600519", text, sizeof(text));
    assert(strcmp(text, "600519") == 0);
    stock_monitor_format_symbol("sz000001", text, sizeof(text));
    assert(strcmp(text, "000001") == 0);
    stock_monitor_format_symbol("600519", text, sizeof(text));
    assert(strcmp(text, "600519") == 0);
}

static void fit_text_scale(void)
{
    assert(stock_monitor_fit_text_scale("1234.56", 3u, 128) == 3u);
    assert(stock_monitor_fit_text_scale("123456.78", 3u, 128) == 2u);
    assert(stock_monitor_fit_text_scale("+100.00%", 2u, 64) == 1u);
    assert(stock_monitor_fit_text_scale("+1.25%", 2u, 72) == 2u);
    assert(stock_monitor_fit_text_scale("", 3u, 128) == 1u);
}

static void format_clock_weekday(void)
{
    char text[16];

    stock_monitor_format_clock_weekday(9, 5, 1, text, sizeof(text));
    assert(strcmp(text, "09:05 MON") == 0);
    stock_monitor_format_clock_weekday(23, 59, 0, text, sizeof(text));
    assert(strcmp(text, "23:59 SUN") == 0);
    stock_monitor_format_clock_weekday(-1, 0, 1, text, sizeof(text));
    assert(strcmp(text, "--:-- ---") == 0);
    stock_monitor_format_clock_weekday(12, 0, 7, text, sizeof(text));
    assert(strcmp(text, "--:-- ---") == 0);

    stock_monitor_format_clock_weekday_from_unix(
        1794359100LL, 8LL * 60LL * 60LL, text, sizeof(text));
    assert(strcmp(text, "09:05 WED") == 0);
    stock_monitor_format_clock_weekday_from_unix(0LL, 0LL, text,
                                                  sizeof(text));
    assert(strcmp(text, "--:-- ---") == 0);
    stock_monitor_format_clock_weekday_from_unix(
        1794359100LL, 15LL * 60LL * 60LL, text, sizeof(text));
    assert(strcmp(text, "--:-- ---") == 0);
}

static void sparkline_points(void)
{
    stock_price_history_t history = {0};
    stock_sparkline_point_t points[8];
    size_t count;

    history.count = 3u;
    history.prices[0] = 10.0f;
    history.prices[1] = 11.0f;
    history.prices[2] = 12.0f;
    count = stock_monitor_sparkline_points(
        &history, 10, 20, 10, 10, points,
        sizeof(points) / sizeof(points[0]));
    assert(count == 3u);
    assert(points[0].x == 10 && points[0].y == 28);
    assert(points[1].x == 14 && points[1].y == 24);
    assert(points[2].x == 19 && points[2].y == 21);

    history.count = 4u;
    history.prices[0] = 5.0f;
    history.prices[1] = 1.0f;
    history.prices[2] = 2.0f;
    history.prices[3] = 3.0f;
    count = stock_monitor_sparkline_points(
        &history, 0, 0, 3, 10, points,
        sizeof(points) / sizeof(points[0]));
    assert(count == 3u);
    assert(points[0].x == 0);
    assert(points[2].x == 2);

    history.count = 1u;
    history.prices[0] = 10.0f;
    count = stock_monitor_sparkline_points(
        &history, 10, 20, 10, 10, points,
        sizeof(points) / sizeof(points[0]));
    assert(count == 1u);
    assert(points[0].x == 19);
    assert(points[0].y == 24);

    history.count = 0u;
    assert(stock_monitor_sparkline_points(
               &history, 0, 0, 10, 10, points,
               sizeof(points) / sizeof(points[0])) == 0u);
}

int main(void)
{
    format_turnover();
    format_symbol();
    fit_text_scale();
    format_clock_weekday();
    sparkline_points();
    return 0;
}
