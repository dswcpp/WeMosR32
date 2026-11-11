#include "stock_intraday_parser.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool is_space(char value)
{
    return value == ' ' || value == '\t';
}

static bool is_digit(char value)
{
    return value >= '0' && value <= '9';
}

static bool parse_number(const char **cursor, double *value)
{
    char *end = NULL;
    double parsed;

    if (cursor == NULL || *cursor == NULL || value == NULL) {
        return false;
    }
    errno = 0;
    parsed = strtod(*cursor, &end);
    if (end == *cursor || (*end != '\0' && !is_space(*end)) ||
        errno == ERANGE || !isfinite(parsed)) {
        return false;
    }
    *cursor = end;
    *value = parsed;
    return true;
}

bool stock_intraday_parse_sample(const char *row,
                                 stock_intraday_sample_t *sample)
{
    const char *cursor;
    stock_intraday_sample_t parsed;
    double price;
    double volume;
    double turnover;
    unsigned hour;
    unsigned minute;

    if (row == NULL || sample == NULL) {
        return false;
    }
    cursor = row;
    while (is_space(*cursor)) {
        cursor++;
    }
    for (size_t index = 0u; index < 4u; index++) {
        if (!is_digit(cursor[index])) {
            return false;
        }
    }
    if (!is_space(cursor[4])) {
        return false;
    }
    hour = (unsigned)(cursor[0] - '0') * 10u +
           (unsigned)(cursor[1] - '0');
    minute = (unsigned)(cursor[2] - '0') * 10u +
             (unsigned)(cursor[3] - '0');
    if (hour > 23u || minute > 59u) {
        return false;
    }
    cursor += 4;
    while (is_space(*cursor)) {
        cursor++;
    }
    if (!parse_number(&cursor, &price) || price <= 0.0) {
        return false;
    }
    while (is_space(*cursor)) {
        cursor++;
    }
    if (!parse_number(&cursor, &volume) || volume < 0.0) {
        return false;
    }
    while (is_space(*cursor)) {
        cursor++;
    }
    if (!parse_number(&cursor, &turnover) || turnover < 0.0) {
        return false;
    }
    while (is_space(*cursor)) {
        cursor++;
    }
    if (*cursor != '\0') {
        return false;
    }

    parsed.minute_of_day = (uint16_t)(hour * 60u + minute);
    parsed.price = (float)price;
    parsed.cumulative_volume = volume;
    parsed.cumulative_turnover = turnover;
    *sample = parsed;
    return true;
}

bool stock_intraday_parse_price(const char *row, float *price)
{
    stock_intraday_sample_t sample;

    if (price == NULL || !stock_intraday_parse_sample(row, &sample)) {
        return false;
    }
    *price = sample.price;
    return true;
}

static bool leap_year(unsigned year)
{
    return (year % 4u == 0u && year % 100u != 0u) || year % 400u == 0u;
}

static unsigned days_in_month(unsigned year, unsigned month)
{
    static const uint8_t days[] = {
        31u, 28u, 31u, 30u, 31u, 30u,
        31u, 31u, 30u, 31u, 30u, 31u,
    };

    if (month == 2u && leap_year(year)) {
        return 29u;
    }
    return days[month - 1u];
}

/* Days since 1970-01-01 for a proleptic Gregorian date. */
static int64_t days_from_civil(int year, unsigned month, unsigned day)
{
    int64_t y = year;
    const unsigned shifted_month = month > 2u ? month - 3u : month + 9u;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned year_of_era = (unsigned)(y - era * 400);
    const unsigned day_of_year =
        (153u * shifted_month + 2u) / 5u + day - 1u;
    const unsigned day_of_era =
        year_of_era * 365u + year_of_era / 4u - year_of_era / 100u +
        day_of_year;

    return era * 146097 + (int64_t)day_of_era - 719468;
}

bool stock_intraday_day_start_utc(const char *date, uint32_t *timestamp_s)
{
    unsigned year;
    unsigned month;
    unsigned day;
    int64_t seconds;

    if (date == NULL || timestamp_s == NULL) {
        return false;
    }
    for (size_t index = 0u; index < 8u; index++) {
        if (!is_digit(date[index])) {
            return false;
        }
    }
    if (date[8] != '\0') {
        return false;
    }
    year = (unsigned)(date[0] - '0') * 1000u +
           (unsigned)(date[1] - '0') * 100u +
           (unsigned)(date[2] - '0') * 10u +
           (unsigned)(date[3] - '0');
    month = (unsigned)(date[4] - '0') * 10u +
            (unsigned)(date[5] - '0');
    day = (unsigned)(date[6] - '0') * 10u +
          (unsigned)(date[7] - '0');
    if (month < 1u || month > 12u || day < 1u ||
        day > days_in_month(year, month)) {
        return false;
    }

    seconds = days_from_civil((int)year, month, day) * 86400 - 8 * 3600;
    if (seconds < 0 || seconds > (int64_t)UINT32_MAX) {
        return false;
    }
    *timestamp_s = (uint32_t)seconds;
    return true;
}

size_t stock_intraday_downsample(const float *samples, size_t sample_count,
                                 float *output, size_t capacity)
{
    size_t output_count;

    if (samples == NULL || output == NULL || sample_count == 0u ||
        capacity == 0u) {
        return 0u;
    }
    output_count = sample_count < capacity ? sample_count : capacity;
    if (sample_count <= capacity) {
        memcpy(output, samples, sample_count * sizeof(samples[0]));
        return sample_count;
    }
    if (output_count == 1u) {
        output[0] = samples[sample_count - 1u];
        return 1u;
    }

    for (size_t index = 0u; index < output_count; index++) {
        size_t source_index =
            index * (sample_count - 1u) / (output_count - 1u);

        output[index] = samples[source_index];
    }
    output[output_count - 1u] = samples[sample_count - 1u];
    return output_count;
}
