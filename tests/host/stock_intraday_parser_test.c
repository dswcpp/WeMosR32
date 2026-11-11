#include "stock_intraday_parser.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void parse_minute_rows(void)
{
    float price = 0.0f;
    stock_intraday_sample_t sample;
    uint32_t day_start = 0u;

    assert(stock_intraday_parse_price(
        "0930 5.68 10294 5846992.00", &price));
    assert(fabsf(price - 5.68f) < 0.001f);
    assert(stock_intraday_parse_sample(
        "0930 5.68 10294 5846992.00", &sample));
    assert(sample.minute_of_day == 9u * 60u + 30u);
    assert(fabsf(sample.price - 5.68f) < 0.001f);
    assert(fabs(sample.cumulative_volume - 10294.0) < 0.001);
    assert(fabs(sample.cumulative_turnover - 5846992.0) < 0.001);
    assert(stock_intraday_parse_price(
        " 1530   5.64 658849 370820347.73", &price));
    assert(fabsf(price - 5.64f) < 0.001f);

    assert(!stock_intraday_parse_price("09300 5.68 1 1", &price));
    assert(!stock_intraday_parse_price("2460 5.68 1 1", &price));
    assert(!stock_intraday_parse_price("0930", &price));
    assert(!stock_intraday_parse_price("0930 -- 1 1", &price));
    assert(!stock_intraday_parse_price("0930 5.68x 1 1", &price));
    assert(!stock_intraday_parse_price("0930 0 1 1", &price));
    assert(!stock_intraday_parse_price(NULL, &price));

    assert(stock_intraday_day_start_utc("20261111", &day_start));
    assert(day_start == 1794326400u);
    assert(!stock_intraday_day_start_utc("20260229", &day_start));
    assert(stock_intraday_day_start_utc("20240229", &day_start));
    assert(!stock_intraday_day_start_utc("20261301", &day_start));
    assert(!stock_intraday_day_start_utc("2026111", &day_start));
}

static void downsample_history(void)
{
    float samples[267];
    float output[64];
    size_t count;

    for (size_t index = 0u; index < 267u; index++) {
        samples[index] = (float)index;
    }
    count = stock_intraday_downsample(
        samples, 267u, output, sizeof(output) / sizeof(output[0]));
    assert(count == 64u);
    assert(output[0] == 0.0f);
    assert(output[count - 1u] == 266.0f);
    for (size_t index = 1u; index < count; index++) {
        assert(output[index] > output[index - 1u]);
    }

    count = stock_intraday_downsample(samples, 3u, output,
                                      sizeof(output) / sizeof(output[0]));
    assert(count == 3u);
    assert(output[0] == 0.0f);
    assert(output[1] == 1.0f);
    assert(output[2] == 2.0f);

    count = stock_intraday_downsample(samples, 267u, output, 1u);
    assert(count == 1u);
    assert(output[0] == 266.0f);
    assert(stock_intraday_downsample(NULL, 10u, output, 10u) == 0u);
    assert(stock_intraday_downsample(samples, 0u, output, 10u) == 0u);
}

int main(void)
{
    parse_minute_rows();
    downsample_history();
    puts("stock intraday parser tests passed");
    return 0;
}
