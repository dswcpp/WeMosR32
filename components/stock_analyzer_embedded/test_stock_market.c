#include "stock_market.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static stock_market_watch_t g_watch;
static stock_market_update_t g_out;

static stock_market_quote_t quote_at(uint32_t ts, float price, uint64_t volume)
{
    stock_market_quote_t q = {0};
    memcpy(q.symbol, "000001", 7);
    q.timestamp_s = ts;
    q.last_price = price;
    q.cumulative_volume_shares = volume;
    return q;
}

static void test_parser(void)
{
    stock_market_quote_t quote;
    stock_market_decoder_t d;
    const char *line = "Q,000001,1794274200,10.25,1234500\r\n";
    size_t i;
    unsigned char checksum = 0;
    char checked[100];
    char corrupt[100];
    assert(stock_market_parse_line(line, &quote) == STOCK_MARKET_OK);
    assert(quote.timestamp_s == 1794274200u);
    assert(quote.cumulative_volume_shares == 1234500u);
    assert(quote.last_price > 10.24f && quote.last_price < 10.26f);
    assert(stock_market_parse_line("Q,000001,1794274200,10.25,-1", &quote) < 0);
    assert(stock_market_parse_line("Q,000001,1794274200,nan,123", &quote) < 0);
    assert(stock_market_parse_line("Q,000001,1794274200,10.25,123,EXTRA", &quote) < 0);
    assert(stock_market_parse_line("Q,000001,1794274200,10.25,18446744073709551616", &quote) < 0);
    assert(stock_market_parse_line("Q,000001,1794274200,10.25,123*00", &quote) < 0);
    strcpy(checked, "Q,000001,1794274200,10.25,123");
    for (i = 0; checked[i]; ++i) checksum ^= (unsigned char)checked[i];
    (void)snprintf(checked + strlen(checked), sizeof(checked) - strlen(checked),
                   "*%02X", (unsigned)checksum);
    assert(stock_market_parse_line(checked, &quote) == STOCK_MARKET_OK);
    strcpy(corrupt, checked);
    corrupt[2] = '1';
    assert(stock_market_parse_line(corrupt, &quote) == STOCK_MARKET_ERR_FORMAT);
    stock_market_decoder_init(&d);
    for (i = 0; i < strlen(line) - 1; ++i)
        assert(stock_market_decoder_push(&d, (uint8_t)line[i], &quote) == STOCK_MARKET_OK);
    assert(stock_market_decoder_push(&d, (uint8_t)'\n', &quote) == STOCK_MARKET_FRAME_READY);
    for (i = 0; i < STOCK_MARKET_FRAME_MAX + 10; ++i)
        (void)stock_market_decoder_push(&d, (uint8_t)'x', &quote);
    assert(stock_market_decoder_push(&d, (uint8_t)'\n', &quote) == STOCK_MARKET_ERR_FORMAT);
    assert(stock_market_parse_line("Q,000001,1794274200,10.25,123", &quote) == STOCK_MARKET_OK);
}

static void test_feed(void)
{
    stock_market_quote_t q;
    stock_analyzer_result_t closed;
    assert(stock_market_init(&g_watch, "000001", NULL) == STOCK_MARKET_OK);
    q = quote_at(1794274200u, 10.0f, 1000u);
    assert(stock_market_feed(&g_watch, &q, &g_out) == STOCK_MARKET_OK);
    assert(g_out.volume_delta_shares == 0.0f);
    assert(stock_market_feed(&g_watch, &q, &g_out) == STOCK_MARKET_DUPLICATE);
    q.timestamp_s += 5u;
    q.last_price = 10.1f;
    q.cumulative_volume_shares = 1300u;
    assert(stock_market_feed(&g_watch, &q, &g_out) == STOCK_MARKET_OK);
    assert(g_out.volume_delta_shares == 300.0f);
    q.timestamp_s += 60u;
    q.last_price = 10.2f;
    q.cumulative_volume_shares = 1800u;
    assert(stock_market_feed(&g_watch, &q, &g_out) == STOCK_MARKET_BAR_CLOSED);
    assert(g_out.has_closed && g_out.closed.is_closed);
    assert(g_out.volume_delta_shares == 500.0f);
    q.timestamp_s += 5u;
    q.cumulative_volume_shares = 100u; /* provider counter reset */
    assert(stock_market_feed(&g_watch, &q, &g_out) == STOCK_MARKET_OK);
    assert(g_out.volume_resynced && g_out.volume_delta_shares == 0.0f);
    q.timestamp_s -= 20u;
    assert(stock_market_feed(&g_watch, &q, &g_out) == STOCK_MARKET_ERR_TIME);
    q = quote_at(1794360600u, 11.0f, 2000u); /* next day */
    assert(stock_market_feed(&g_watch, &q, &g_out) == STOCK_MARKET_BAR_CLOSED);
    assert(g_out.new_day && g_out.has_closed);
    assert(g_out.volume_delta_shares == 0.0f);
    assert(stock_market_flush(&g_watch, &closed) == STOCK_MARKET_BAR_CLOSED);
    assert(stock_market_flush(&g_watch, &closed) == STOCK_MARKET_OK);
    strcpy(q.symbol, "600519");
    assert(stock_market_feed(&g_watch, &q, &g_out) == STOCK_MARKET_ERR_SYMBOL);
}

static void test_sessions(void)
{
    assert(stock_market_is_cn_regular_session(1794274200u));
    assert(!stock_market_is_cn_regular_session(1794281400u));
    assert(stock_market_is_cn_regular_session(1794286800u));
    assert(!stock_market_is_cn_regular_session(1794294000u));
    assert(!stock_market_is_cn_regular_session(1794619800u));
}

int main(void)
{
    test_parser();
    test_feed();
    test_sessions();
    printf("stock_market tests passed; watch RAM=%lu bytes\n",
           (unsigned long)sizeof(stock_market_watch_t));
    return 0;
}
