#include "stock_quote_parser.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define FIELD_COUNT 40u

static size_t append_text(char *output, size_t capacity, size_t used,
                          const char *text)
{
    size_t length = strlen(text);

    assert(used + length < capacity);
    memcpy(output + used, text, length + 1u);
    return used + length;
}

static void build_quote_response(char *output, size_t capacity)
{
    const char *fields[FIELD_COUNT] = {0};
    size_t used = 0u;

    for (size_t index = 0u; index < FIELD_COUNT; index++) {
        fields[index] = "0";
    }
    fields[3] = "1523.45";
    fields[4] = "1500.10";
    fields[5] = "1505.20";
    fields[6] = "123456";
    fields[29] = "";
    fields[30] = "20261110150000";
    fields[31] = "23.35";
    fields[32] = "1.56";
    fields[33] = "1540.00";
    fields[34] = "1492.80";
    fields[37] = "37031";

    used = append_text(output, capacity, used,
                       "v_sh600519=\"");
    for (size_t index = 0u; index < FIELD_COUNT; index++) {
        if (index > 0u) {
            used = append_text(output, capacity, used, "~");
        }
        used = append_text(output, capacity, used, fields[index]);
    }
    (void)append_text(output, capacity, used, "\";\r\n");
}

static void normalize_symbols(void)
{
    char output[STOCK_SYMBOL_MAX_LEN + 1u];

    assert(stock_symbol_normalize(" 600519 ", output));
    assert(strcmp(output, "sh600519") == 0);
    assert(stock_symbol_normalize("000001", output));
    assert(strcmp(output, "sz000001") == 0);
    assert(stock_symbol_normalize("SH000001", output));
    assert(strcmp(output, "sh000001") == 0);
    assert(stock_symbol_normalize("399006", output));
    assert(strcmp(output, "sz399006") == 0);
    assert(stock_symbol_normalize("512800", output));
    assert(strcmp(output, "sh512800") == 0);
    assert(stock_symbol_normalize("sz512800", output));
    assert(strcmp(output, "sh512800") == 0);
    assert(!stock_symbol_normalize("60051", output));
    assert(!stock_symbol_normalize("xx600519", output));
}

static void parse_tencent_quote(void)
{
    char response[1024];
    stock_quote_t quotes[1];
    stock_quote_t *quote = &quotes[0];
    size_t count = 0u;

    build_quote_response(response, sizeof(response));
    assert(stock_quote_parser_parse(response, quotes, 1u, &count));
    assert(count == 1u);
    assert(strcmp(quote->symbol, "sh600519") == 0);
    assert(strcmp(quote->code, "600519") == 0);
    assert(strcmp(quote->updated_at, "20261110150000") == 0);
    assert(quote->valid);
    assert(fabs(quote->price - 1523.45) < 0.001);
    assert(fabs(quote->previous_close - 1500.10) < 0.001);
    assert(fabs(quote->open - 1505.20) < 0.001);
    assert(fabs(quote->volume_lots - 123456.0) < 0.001);
    assert(fabs(quote->change - 23.35) < 0.001);
    assert(fabs(quote->change_percent - 1.56) < 0.001);
    assert(fabs(quote->high - 1540.00) < 0.001);
    assert(fabs(quote->low - 1492.80) < 0.001);
    assert(fabs(quote->turnover_cny - 370310000.0) < 0.001);
}

static void reject_invalid_records(void)
{
    stock_quote_t quotes[1];
    size_t count = 99u;

    assert(!stock_quote_parser_parse(
        "v_sh600519=1~bad~record", quotes, 1u, &count));
    assert(count == 0u);
    assert(!stock_quote_parser_parse(
        "x_sh600519=\"1\";", quotes, 1u, &count));
    assert(count == 0u);
}

int main(void)
{
    normalize_symbols();
    parse_tencent_quote();
    reject_invalid_records();
    puts("stock quote parser tests passed");
    return 0;
}
