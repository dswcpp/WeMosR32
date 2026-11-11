#include "stock_monitor_visual.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void stock_monitor_format_turnover(double turnover_cny, char *buffer,
                                   size_t size)
{
    if (buffer == NULL || size == 0u) {
        return;
    }
    if (!isfinite(turnover_cny) || turnover_cny <= 0.0) {
        (void)snprintf(buffer, size, "AMT N/A");
    } else if (turnover_cny >= 1000000000.0) {
        (void)snprintf(buffer, size, "AMT %.2fB", turnover_cny / 1e9);
    } else if (turnover_cny >= 1000000.0) {
        (void)snprintf(buffer, size, "AMT %.1fM", turnover_cny / 1e6);
    } else if (turnover_cny >= 1000.0) {
        (void)snprintf(buffer, size, "AMT %.1fK", turnover_cny / 1e3);
    } else {
        (void)snprintf(buffer, size, "AMT %.0f", turnover_cny);
    }
}

void stock_monitor_format_symbol(const char *symbol, char *buffer,
                                 size_t size)
{
    const char *code = symbol;
    size_t length;

    if (buffer == NULL || size == 0u) {
        return;
    }
    if (symbol == NULL) {
        buffer[0] = '\0';
        return;
    }

    length = strlen(symbol);
    if (length == 8u &&
        (strncmp(symbol, "sh", 2u) == 0 ||
         strncmp(symbol, "sz", 2u) == 0 ||
         strncmp(symbol, "bj", 2u) == 0)) {
        code = symbol + 2u;
    }
    (void)snprintf(buffer, size, "%s", code);
}

uint8_t stock_monitor_fit_text_scale(const char *text, uint8_t max_scale,
                                     int max_width)
{
    size_t length;
    uint8_t scale;

    if (text == NULL || text[0] == '\0' || max_width <= 0) {
        return 1u;
    }
    if (max_scale == 0u) {
        max_scale = 1u;
    }

    length = strlen(text);
    for (scale = max_scale; scale > 1u; scale--) {
        int width = (int)(length * 6u * scale) - (int)scale;

        if (width <= max_width) {
            return scale;
        }
    }
    return 1u;
}

void stock_monitor_format_clock_weekday(int hour, int minute, int weekday,
                                        char *buffer, size_t size)
{
    static const char *const weekdays[] = {
        "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT",
    };

    if (buffer == NULL || size == 0u) {
        return;
    }
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        weekday < 0 || weekday > 6) {
        (void)snprintf(buffer, size, "--:-- ---");
        return;
    }
    (void)snprintf(buffer, size, "%02d:%02d %s", hour, minute,
                   weekdays[weekday]);
}

void stock_monitor_format_clock_weekday_from_unix(
    int64_t unix_seconds, int64_t utc_offset_s, char *buffer, size_t size)
{
    const int64_t seconds_per_day = 86400LL;
    const int64_t max_utc_offset_s = 14LL * 60LL * 60LL;
    int64_t local_seconds;
    int64_t days;
    int64_t seconds_of_day;
    int weekday;
    int hour;
    int minute;

    if (buffer == NULL || size == 0u) {
        return;
    }
    if (unix_seconds <= 0 ||
        utc_offset_s < -max_utc_offset_s ||
        utc_offset_s > max_utc_offset_s ||
        (utc_offset_s > 0 &&
         unix_seconds > INT64_MAX - utc_offset_s)) {
        stock_monitor_format_clock_weekday(-1, 0, 0, buffer, size);
        return;
    }

    local_seconds = unix_seconds + utc_offset_s;
    days = local_seconds / seconds_per_day;
    seconds_of_day = local_seconds % seconds_per_day;
    if (seconds_of_day < 0) {
        seconds_of_day += seconds_per_day;
        days--;
    }

    weekday = (int)((days + 4LL) % 7LL);
    if (weekday < 0) {
        weekday += 7;
    }
    hour = (int)(seconds_of_day / 3600LL);
    minute = (int)((seconds_of_day % 3600LL) / 60LL);
    stock_monitor_format_clock_weekday(hour, minute, weekday, buffer, size);
}

size_t stock_monitor_sparkline_points(
    const stock_price_history_t *history, int x, int y, int width,
    int height, stock_sparkline_point_t *points, size_t capacity)
{
    float valid[STOCK_PRICE_HISTORY_MAX];
    size_t valid_count = 0u;
    size_t plotted_count;
    size_t first;
    float minimum;
    float maximum;
    float range;
    int plot_y = y;
    int plot_height = height;

    if (history == NULL || points == NULL || width < 2 || height < 2) {
        return 0u;
    }
    for (size_t index = 0u;
         index < history->count && index < STOCK_PRICE_HISTORY_MAX;
         index++) {
        float price = history->prices[index];

        if (isfinite(price) && price > 0.0f) {
            valid[valid_count++] = price;
        }
    }
    if (valid_count == 0u) {
        return 0u;
    }
    plotted_count = valid_count < (size_t)width
                        ? valid_count
                        : (size_t)width;
    if (capacity < plotted_count) {
        return 0u;
    }
    first = valid_count - plotted_count;
    minimum = valid[first];
    maximum = valid[first];
    for (size_t index = first + 1u; index < valid_count; index++) {
        if (valid[index] < minimum) {
            minimum = valid[index];
        }
        if (valid[index] > maximum) {
            maximum = valid[index];
        }
    }
    range = maximum - minimum;
    if (height >= 4) {
        plot_y = y + 1;
        plot_height = height - 2;
    }

    for (size_t index = 0u; index < plotted_count; index++) {
        float price = valid[first + index];
        double position;
        int offset;

        points[index].x =
            plotted_count == 1u
                ? x + width - 1
                : x + (int)(index * (size_t)(width - 1) /
                            (plotted_count - 1u));
        if (range <= 0.0f) {
            points[index].y =
                plot_y + (plot_height - 1) / 2;
        } else {
            position = (double)(price - minimum) / (double)range;
            offset = (int)(position * (double)(plot_height - 1) + 0.5);
            points[index].y = plot_y + plot_height - 1 - offset;
        }
    }
    return plotted_count;
}
