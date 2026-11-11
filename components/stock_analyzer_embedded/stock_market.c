#include "stock_market.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int symbol_valid(const char *s)
{
    unsigned i;
    if (!s) return 0;
    for (i = 0u; i < STOCK_MARKET_SYMBOL_LEN; ++i) {
        if (s[i] < '0' || s[i] > '9') return 0;
    }
    return s[STOCK_MARKET_SYMBOL_LEN] == '\0';
}

static uint32_t china_day(uint32_t timestamp_s)
{
    return (uint32_t)(((uint64_t)timestamp_s + 8u * 3600u) / 86400u);
}

stock_market_status_t stock_market_init(stock_market_watch_t *watch,
                                        const char *symbol,
                                        const stock_analyzer_config_t *cfg)
{
    stock_analyzer_config_t defaults;
    if (!watch || !symbol_valid(symbol))
        return watch ? STOCK_MARKET_ERR_SYMBOL : STOCK_MARKET_ERR_ARGUMENT;
    memset(watch, 0, sizeof(*watch));
    memcpy(watch->symbol, symbol, STOCK_MARKET_SYMBOL_LEN + 1u);
    if (!cfg) {
        stock_analyzer_default_config(&defaults);
        cfg = &defaults;
    }
    if (stock_analyzer_init(&watch->analyzer, cfg) < 0)
        return STOCK_MARKET_ERR_ANALYZER;
    watch->initialized = 1u;
    return STOCK_MARKET_OK;
}

stock_market_status_t stock_market_feed(stock_market_watch_t *watch,
                                        const stock_market_quote_t *quote,
                                        stock_market_update_t *out)
{
    stock_tick_t tick;
    stock_analyzer_status_t st;
    uint32_t day;
    uint64_t delta = 0u;

    if (!watch || !quote || !out || !watch->initialized)
        return STOCK_MARKET_ERR_ARGUMENT;
    if (!symbol_valid(quote->symbol) || strcmp(watch->symbol, quote->symbol) != 0)
        return STOCK_MARKET_ERR_SYMBOL;
    if (quote->timestamp_s == 0u || !isfinite(quote->last_price) ||
        quote->last_price <= 0.0f)
        return STOCK_MARKET_ERR_RANGE;
    day = china_day(quote->timestamp_s);
    if (watch->has_quote && quote->timestamp_s < watch->last_timestamp_s)
        return STOCK_MARKET_ERR_TIME;
    if (watch->has_quote && quote->timestamp_s == watch->last_timestamp_s &&
        quote->last_price == watch->last_price &&
        quote->cumulative_volume_shares == watch->last_volume_shares)
        return STOCK_MARKET_DUPLICATE;

    memset(out, 0, sizeof(*out));
    if (watch->has_quote && day != watch->day_key) {
        /* Preserve prior day's last bar before resetting intraday VWAP. */
        st = stock_analyzer_flush_tick(&watch->analyzer, &out->closed);
        if (st < 0) return STOCK_MARKET_ERR_ANALYZER;
        out->has_closed = (uint8_t)(st == STOCK_ANALYZER_BAR_COMPLETED);
        if (stock_analyzer_reset_session(&watch->analyzer) < 0)
            return STOCK_MARKET_ERR_ANALYZER;
        out->new_day = 1u;
    } else if (watch->has_quote) {
        if (quote->cumulative_volume_shares >= watch->last_volume_shares) {
            delta = quote->cumulative_volume_shares - watch->last_volume_shares;
        } else {
            /* Source reset/inconsistent volume: reseed; don't fake huge volume. */
            out->volume_resynced = 1u;
        }
    }

    /* First quote of a new day only seeds a cumulative baseline.
     * It cannot reconstruct earlier unobserved intraday turnover. */
    tick.timestamp_s = quote->timestamp_s;
    tick.price = quote->last_price;
    tick.volume_delta = (float)delta;
    st = stock_analyzer_feed_tick(&watch->analyzer, &tick,
                                  &out->live,
                                  out->has_closed ? NULL : &out->closed);
    if (st < 0) return STOCK_MARKET_ERR_ANALYZER;
    if (st == STOCK_ANALYZER_BAR_COMPLETED) out->has_closed = 1u;

    watch->has_quote = 1u;
    watch->day_key = day;
    watch->last_timestamp_s = quote->timestamp_s;
    watch->last_volume_shares = quote->cumulative_volume_shares;
    watch->last_price = quote->last_price;
    out->volume_delta_shares = (float)delta;
    return out->has_closed ? STOCK_MARKET_BAR_CLOSED : STOCK_MARKET_OK;
}

stock_market_status_t stock_market_flush(stock_market_watch_t *watch,
                                         stock_analyzer_result_t *closed_out)
{
    stock_analyzer_status_t ret;
    if (!watch || !watch->initialized || !closed_out)
        return STOCK_MARKET_ERR_ARGUMENT;
    ret = stock_analyzer_flush_tick(&watch->analyzer, closed_out);
    if (ret < 0) return STOCK_MARKET_ERR_ANALYZER;
    return ret == STOCK_ANALYZER_BAR_COMPLETED ?
           STOCK_MARKET_BAR_CLOSED : STOCK_MARKET_OK;
}

static int take_separator(const char **p)
{
    if (**p != ',') return 0;
    ++(*p);
    return 1;
}

static int parse_u64(const char **p, uint64_t *out)
{
    unsigned long long value;
    char *end;
    if (**p < '0' || **p > '9') return 0;
    errno = 0;
    value = strtoull(*p, &end, 10);
    if (errno == ERANGE || end == *p) return 0;
    *p = end;
    *out = (uint64_t)value;
    return 1;
}

static int parse_price(const char **p, float *out)
{
    double value;
    char *end;
    if ((**p < '0' || **p > '9') && **p != '.') return 0;
    errno = 0;
    value = strtod(*p, &end);
    if (errno == ERANGE || end == *p || !isfinite(value) ||
        value <= 0.0 || value > 100000000.0) return 0;
    *p = end;
    *out = (float)value;
    return isfinite(*out) && *out > 0.0f;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

stock_market_status_t stock_market_parse_line(const char *line,
                                              stock_market_quote_t *quote)
{
    const char *p;
    const char *q;
    size_t len;
    uint64_t value;
    uint8_t check = 0u;
    stock_market_quote_t parsed;
    unsigned i;
    if (!line || !quote) return STOCK_MARKET_ERR_ARGUMENT;
    len = strlen(line);
    if (len == 0u || len >= STOCK_MARKET_FRAME_MAX)
        return STOCK_MARKET_ERR_FORMAT;
    memset(&parsed, 0, sizeof(parsed));
    p = line;
    if (p[0] != 'Q' || p[1] != ',') return STOCK_MARKET_ERR_FORMAT;
    p += 2;
    for (i = 0u; i < STOCK_MARKET_SYMBOL_LEN; ++i) {
        if (p[i] < '0' || p[i] > '9') return STOCK_MARKET_ERR_FORMAT;
        parsed.symbol[i] = p[i];
    }
    p += STOCK_MARKET_SYMBOL_LEN;
    if (!take_separator(&p) || !parse_u64(&p, &value) ||
        value == 0u || value > UINT32_MAX) return STOCK_MARKET_ERR_RANGE;
    parsed.timestamp_s = (uint32_t)value;
    if (!take_separator(&p) || !parse_price(&p, &parsed.last_price) ||
        !take_separator(&p) ||
        !parse_u64(&p, &parsed.cumulative_volume_shares))
        return STOCK_MARKET_ERR_FORMAT;
    if (*p == '*') {
        int hi, lo;
        q = line;
        while (q < p) check ^= (uint8_t)*q++;
        ++p;
        hi = hexval(p[0]);
        lo = hi < 0 || p[0] == '\0' ? -1 : hexval(p[1]);
        if (hi < 0 || lo < 0 || (uint8_t)((hi << 4) | lo) != check)
            return STOCK_MARKET_ERR_FORMAT;
        p += 2;
    }
    if (*p == '\r') ++p;
    if (*p == '\n') ++p;
    if (*p != '\0') return STOCK_MARKET_ERR_FORMAT;
    *quote = parsed;
    return STOCK_MARKET_OK;
}

void stock_market_decoder_init(stock_market_decoder_t *decoder)
{
    if (decoder) memset(decoder, 0, sizeof(*decoder));
}

stock_market_status_t stock_market_decoder_push(stock_market_decoder_t *decoder,
                                                uint8_t byte,
                                                stock_market_quote_t *quote)
{
    stock_market_status_t status;
    if (!decoder || !quote) return STOCK_MARKET_ERR_ARGUMENT;
    if (byte == (uint8_t)'\n') {
        if (decoder->overflow || decoder->length == 0u) {
            decoder->length = decoder->overflow = 0u;
            return STOCK_MARKET_ERR_FORMAT;
        }
        decoder->data[decoder->length] = '\0';
        status = stock_market_parse_line(decoder->data, quote);
        decoder->length = decoder->overflow = 0u;
        return status == STOCK_MARKET_OK ? STOCK_MARKET_FRAME_READY : status;
    }
    if (decoder->overflow) return STOCK_MARKET_OK;
    if (decoder->length >= STOCK_MARKET_FRAME_MAX - 1u) {
        decoder->overflow = 1u;
        return STOCK_MARKET_OK;
    }
    decoder->data[decoder->length++] = (char)byte;
    return STOCK_MARKET_OK;
}

int stock_market_is_cn_regular_session(uint32_t utc_s)
{
    uint64_t local = (uint64_t)utc_s + 8u * 3600u;
    uint32_t day = (uint32_t)(local / 86400u);
    uint32_t weekday = (day + 4u) % 7u; /* 0=Sunday, 6=Saturday */
    uint32_t minute = (uint32_t)((local % 86400u) / 60u);
    if (weekday == 0u || weekday == 6u) return 0;
    return (minute >= 9u * 60u + 30u && minute < 11u * 60u + 30u) ||
           (minute >= 13u * 60u && minute < 15u * 60u);
}
