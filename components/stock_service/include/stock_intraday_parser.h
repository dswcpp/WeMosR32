#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t minute_of_day;
    float price;
    double cumulative_volume;
    double cumulative_turnover;
} stock_intraday_sample_t;

/*
 * Parses one Tencent minute-history row:
 * "HHMM price volume turnover".
 */
bool stock_intraday_parse_sample(const char *row,
                                 stock_intraday_sample_t *sample);
bool stock_intraday_parse_price(const char *row, float *price);

/*
 * Converts an Asia/Shanghai YYYYMMDD session date to the Unix UTC second
 * at local midnight.
 */
bool stock_intraday_day_start_utc(const char *date, uint32_t *timestamp_s);

/*
 * Copies all samples when they fit. Otherwise selects evenly spaced samples
 * from the beginning through the latest value, inclusive.
 */
size_t stock_intraday_downsample(const float *samples, size_t sample_count,
                                 float *output, size_t capacity);

#ifdef __cplusplus
}
#endif
