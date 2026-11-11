/* STM32 / GD32 bare-metal integration sketch.
 * UART DMA/IRQ receives bytes into YOUR ring buffer; this example is called
 * in the main loop/task after the ring buffer yields a byte (never in ISR).
 * Feed data from a PC/ESP32 adapter, one CSV line per quote:
 * Q,000001,1794274200,10.25,1234500*HH\n
 * Replace on_display() with OLED driver/serial output.
 */
#include "stock_market.h"

static stock_market_watch_t g_stock;
static stock_market_decoder_t g_decoder;

static void on_display(const stock_market_update_t *u)
{
    (void)u;
    /* Example: display price, trend, score on OLED/USART. */
}

void market_uart_init(void)
{
    stock_market_decoder_init(&g_decoder);
    (void)stock_market_init(&g_stock, "000001", NULL);
}

void market_uart_on_byte_from_main_loop(uint8_t byte)
{
    stock_market_quote_t quote;
    stock_market_update_t update;
    stock_market_status_t st = stock_market_decoder_push(
        &g_decoder, byte, &quote);
    if (st != STOCK_MARKET_FRAME_READY) return;
    st = stock_market_feed(&g_stock, &quote, &update);
    if (st == STOCK_MARKET_OK || st == STOCK_MARKET_BAR_CLOSED)
        on_display(&update);
}
