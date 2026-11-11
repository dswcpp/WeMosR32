/* Desktop simulated bridge; compile with stock_market.c stock_analyzer.c -lm */
#include <stdio.h>
#include <string.h>
#include "stock_market.h"

static stock_market_watch_t watch;
static stock_market_decoder_t dec;

int main(void)
{
    const char *frames[] = {
        "Q,000001,1794274200,10.10,1234500\n",
        "Q,000001,1794274205,10.11,1234800\n",
        "Q,000001,1794274265,10.15,1240000\n"
    };
    unsigned i;
    if (stock_market_init(&watch, "000001", NULL) != STOCK_MARKET_OK)
        return 1;
    stock_market_decoder_init(&dec);
    for (i = 0u; i < sizeof(frames) / sizeof(frames[0]); ++i) {
        size_t j;
        for (j = 0u; j < strlen(frames[i]); ++j) {
            stock_market_quote_t quote;
            stock_market_update_t update;
            stock_market_status_t st = stock_market_decoder_push(
                &dec, (uint8_t)frames[i][j], &quote);
            if (st != STOCK_MARKET_FRAME_READY) continue;
            st = stock_market_feed(&watch, &quote, &update);
            if (st < 0) return 2;
            printf("%s price=%.2f delta=%.0f trend=%d score=%d\n",
                   quote.symbol, (double)update.live.close,
                   (double)update.volume_delta_shares,
                   (int)update.live.trend, (int)update.live.score);
            if (update.has_closed)
                printf("Previous minute closed at %.2f\n",
                       (double)update.closed.close);
        }
    }
    return 0;
}
