#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "stock_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int x;
    int y;
} stock_sparkline_point_t;

void stock_monitor_format_turnover(double turnover_cny, char *buffer,
                                   size_t size);

void stock_monitor_format_symbol(const char *symbol, char *buffer,
                                 size_t size);

uint8_t stock_monitor_fit_text_scale(const char *text, uint8_t max_scale,
                                     int max_width);

void stock_monitor_format_clock_weekday(int hour, int minute, int weekday,
                                        char *buffer, size_t size);

void stock_monitor_format_clock_weekday_from_unix(
    int64_t unix_seconds, int64_t utc_offset_s, char *buffer, size_t size);

size_t stock_monitor_sparkline_points(
    const stock_price_history_t *history, int x, int y, int width,
    int height, stock_sparkline_point_t *points, size_t capacity);

#ifdef __cplusplus
}
#endif
