/* ESP-IDF 5.x INTEGRATION EXAMPLE (not part of portable C99 build).
 * Requires: Wi-Fi connected, SNTP time synced, relay_eastmoney.py on LAN.
 * Call esp_stock_init() once, esp_stock_poll_once() every 5-30 seconds
 * in a FreeRTOS task (NOT from ISR or GUI task).
 * Set RELAY_URL to your computer's LAN IP. Use private LAN only.
 * ESP-IDF component REQUIRES: esp_http_client, esp_event, esp_netif,
 * and the stock_market.c and stock_analyzer.c sources.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_http_client.h"
#include "esp_log.h"
#include "stock_market.h"

#define RELAY_URL "http://192.168.1.100:8765/quote?symbol=000001"

static const char *TAG = "stock";
static stock_market_watch_t g_watch;

typedef struct {
    char line[STOCK_MARKET_FRAME_MAX];
    size_t len;
    int overflow;
} response_buffer_t;

static esp_err_t http_event(esp_http_client_event_t *evt)
{
    response_buffer_t *rx = (response_buffer_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && rx && !rx->overflow) {
        if (evt->data_len < 0 ||
            (size_t)evt->data_len >= sizeof(rx->line) - rx->len) {
            rx->overflow = 1;
        } else {
            memcpy(rx->line + rx->len, evt->data, (size_t)evt->data_len);
            rx->len += (size_t)evt->data_len;
        }
    }
    return ESP_OK;
}

int esp_stock_init(void)
{
    return stock_market_init(&g_watch, "000001", NULL) == STOCK_MARKET_OK;
}

void esp_stock_poll_once(void)
{
    response_buffer_t rx = {0};
    stock_market_quote_t quote;
    stock_market_update_t updated;
    stock_market_status_t ret;
    time_t now = time(NULL);
    esp_http_client_config_t config = {
        .url = RELAY_URL,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000,
        .disable_auto_redirect = true,
        .event_handler = http_event,
        .user_data = &rx,
    };
    esp_http_client_handle_t client;
    esp_err_t err;

    if (now < 1700000000 || !stock_market_is_cn_regular_session((uint32_t)now))
        return; /* NTP not synced, or outside weekday trading windows */

    client = esp_http_client_init(&config);
    if (!client) return;
    err = esp_http_client_perform(client);
    if (err != ESP_OK || esp_http_client_get_status_code(client) != 200 ||
        rx.overflow || rx.len == 0u) {
        ESP_LOGW(TAG, "quote HTTP failure (%s)", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return;
    }
    rx.line[rx.len] = '\0';
    esp_http_client_cleanup(client);

    if (stock_market_parse_line(rx.line, &quote) != STOCK_MARKET_OK)
        return;
    /* Reject stale quotes: upstream time is quote time, not request time. */
    if (quote.timestamp_s > (uint32_t)now) {
        if (quote.timestamp_s - (uint32_t)now > 30u) return;
    } else if ((uint32_t)now - quote.timestamp_s > 120u) {
        return;
    }
    ret = stock_market_feed(&g_watch, &quote, &updated);
    if (ret == STOCK_MARKET_DUPLICATE) return;
    if (ret < 0) {
        ESP_LOGW(TAG, "invalid quote: %d", (int)ret);
        return;
    }
    ESP_LOGI(TAG, "close=%.3f trend=%d score=%d delta=%.0f shares",
             (double)updated.live.close, (int)updated.live.trend,
             (int)updated.live.score, (double)updated.volume_delta_shares);
    if (updated.has_closed) {
        /* Updated.closed is the finalized previous minute bar. */
    }
}

void esp_stock_flush_at_session_end(void)
{
    stock_analyzer_result_t closed;
    if (stock_market_flush(&g_watch, &closed) == STOCK_MARKET_BAR_CLOSED) {
        ESP_LOGI(TAG, "final bar close=%.3f", (double)closed.close);
    }
}
