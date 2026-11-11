#ifndef STOCK_MARKET_H
#define STOCK_MARKET_H

/* Portable A-share market quote ingestion for stock_analyzer.h (C99).
 * No networking, RTOS, threads, heap allocation or vendor SDK dependency.
 * Each watch is independent. Callers serialize access to each watch.
 */
#include <stddef.h>
#include <stdint.h>
#include "stock_analyzer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STOCK_MARKET_SYMBOL_LEN 6u
#define STOCK_MARKET_FRAME_MAX 96u

typedef enum {
    STOCK_MARKET_OK = 0,
    STOCK_MARKET_BAR_CLOSED = 1,   /* update.closed is valid */
    STOCK_MARKET_DUPLICATE = 2,    /* duplicate quote ignored */
    STOCK_MARKET_FRAME_READY = 3,  /* serial decoder emitted quote */
    STOCK_MARKET_ERR_ARGUMENT = -1,
    STOCK_MARKET_ERR_SYMBOL = -2,
    STOCK_MARKET_ERR_FORMAT = -3,
    STOCK_MARKET_ERR_RANGE = -4,
    STOCK_MARKET_ERR_TIME = -5,
    STOCK_MARKET_ERR_ANALYZER = -6
} stock_market_status_t;

/* Data source adapters MUST normalize: price to CNY/share,
 * volume to cumulative SHARES since today's session start,
 * timestamp to Unix UTC SECONDS of the ACTUAL quote (not receive time).
 * Chinese stock symbols are six ASCII digits; no implicit symbol routing.
 */
typedef struct {
    char symbol[STOCK_MARKET_SYMBOL_LEN + 1u];
    uint32_t timestamp_s;
    float last_price;
    uint64_t cumulative_volume_shares;
} stock_market_quote_t;

typedef struct {
    stock_analyzer_result_t live;    /* valid on OK/BAR_CLOSED */
    stock_analyzer_result_t closed;  /* valid only when has_closed == 1 */
    float volume_delta_shares;       /* actual shares since previous sample */
    uint8_t has_closed;
    uint8_t new_day;
    uint8_t volume_resynced;         /* cumulative volume went down unexpectedly */
} stock_market_update_t;

typedef struct {
    stock_analyzer_t analyzer;
    char symbol[STOCK_MARKET_SYMBOL_LEN + 1u];
    uint64_t last_volume_shares;
    uint32_t last_timestamp_s;
    uint32_t day_key;
    float last_price;
    uint8_t has_quote;
    uint8_t initialized;
} stock_market_watch_t;

/* CSV frame example: Q,000001,1794274200,10.25,1234500*HH\n
 * The optional *HH is an XOR checksum (uppercase hex, bytes from Q to
 * the character BEFORE '*'). For UART, send it and validate corruption.
 */
typedef struct {
    char data[STOCK_MARKET_FRAME_MAX];
    uint16_t length;
    uint8_t overflow;
} stock_market_decoder_t;

stock_market_status_t stock_market_init(stock_market_watch_t *watch,
                                        const char *symbol,
                                        const stock_analyzer_config_t *cfg);
stock_market_status_t stock_market_feed(stock_market_watch_t *watch,
                                        const stock_market_quote_t *quote,
                                        stock_market_update_t *out);
/* Flush current incomplete minute at lunch / day close. */
stock_market_status_t stock_market_flush(stock_market_watch_t *watch,
                                         stock_analyzer_result_t *closed_out);

/* Parse one complete line (newline optional). No internal heap allocation. */
stock_market_status_t stock_market_parse_line(const char *line,
                                              stock_market_quote_t *quote);
void stock_market_decoder_init(stock_market_decoder_t *decoder);
/* Push received bytes in a NON-ISR task/loop. Returns FRAME_READY on a
 * complete valid line, OK while buffering, <0 on invalid frame/overflow. */
stock_market_status_t stock_market_decoder_push(stock_market_decoder_t *decoder,
                                                uint8_t byte,
                                                stock_market_quote_t *quote);

/* Only weekday clock windows, Asia/Shanghai (UTC+8), NOT holiday-aware.
 * Trading 09:30-11:30, 13:00-15:00; quote endpoint may update later.
 */
int stock_market_is_cn_regular_session(uint32_t utc_s);

#ifdef __cplusplus
}
#endif
#endif
