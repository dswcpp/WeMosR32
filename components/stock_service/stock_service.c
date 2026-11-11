#include "stock_service.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "config_service.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "stock_intraday_parser.h"
#include "stock_intraday_seed.h"
#include "stock_market.h"
#include "stock_quote_parser.h"
#include "wifi_manager.h"

#define STOCK_SERVICE_TASK_STACK 8192u
#define STOCK_SERVICE_TASK_PRIORITY 4u
#define STOCK_SERVICE_POLL_MS 250u
#define STOCK_SERVICE_HTTP_TIMEOUT_MS 6000u
#define STOCK_SERVICE_RESPONSE_MAX 4096u
#define STOCK_SERVICE_HISTORY_RESPONSE_MAX 16384u
#define STOCK_SERVICE_INTRADAY_SOURCE_MAX 512u
#define STOCK_SERVICE_INTRADAY_SEED_MAX 64u
#define STOCK_SERVICE_URL_MAX 192u
#define STOCK_SERVICE_HOST "qt.gtimg.cn"
#define STOCK_SERVICE_PRIMARY_URL "http://" STOCK_SERVICE_HOST "/"
#define STOCK_SERVICE_FALLBACK_URL "http://119.147.12.144/"
#define STOCK_SERVICE_INTRADAY_HOST "web.ifzq.gtimg.cn"
#define STOCK_SERVICE_INTRADAY_PRIMARY_URL \
    "https://" STOCK_SERVICE_INTRADAY_HOST "/appstock/app/minute/query?code="
#define STOCK_SERVICE_CONFIG_MAGIC 0x53544B31u
#define STOCK_SERVICE_CONFIG_VERSION 3u
#define STOCK_SERVICE_DEFAULT_REFRESH_SECONDS 30u
#define STOCK_SERVICE_STALE_FLOOR_SECONDS 300u
#define STOCK_ANALYSIS_VALID_EPOCH 1704067200u

static const char *TAG = "stock_service";
static const char *const s_default_symbols[] = {
    "sh000001",
    "sz399001",
    "sz399006",
};

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t refresh_seconds;
    uint8_t symbol_count;
    uint8_t feedback_mode;
    uint8_t alerts_enabled;
    uint8_t reserved;
    uint32_t alert_upper_price_units[STOCK_SERVICE_MAX_SYMBOLS];
    uint32_t alert_lower_price_units[STOCK_SERVICE_MAX_SYMBOLS];
    char symbols[STOCK_SERVICE_MAX_SYMBOLS][STOCK_SYMBOL_MAX_LEN + 1u];
} stock_persisted_config_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t refresh_seconds;
    uint8_t symbol_count;
    uint8_t feedback_mode;
    uint8_t alerts_enabled;
    uint8_t reserved;
    uint16_t alert_up_threshold_basis_points;
    uint16_t alert_down_threshold_basis_points;
    char symbols[STOCK_SERVICE_MAX_SYMBOLS][STOCK_SYMBOL_MAX_LEN + 1u];
} stock_persisted_config_legacy_t;

typedef union {
    stock_persisted_config_t current;
    stock_persisted_config_legacy_t legacy;
} stock_persisted_config_buffer_t;

typedef struct {
    bool valid;
    uint64_t updated_at_us;
} stock_quote_meta_t;

typedef struct {
    char *data;
    size_t capacity;
    size_t length;
    bool truncated;
} http_response_buffer_t;

_Static_assert(sizeof(stock_persisted_config_t) <=
                   CONFIG_SERVICE_BLOB_MAX_STOCK_SETTINGS,
               "stock settings exceed the NVS storage contract");
_Static_assert(sizeof(stock_persisted_config_legacy_t) <
                   sizeof(stock_persisted_config_t),
               "legacy stock settings must remain smaller than current");

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static stock_service_config_t s_config;
static stock_quote_t s_quotes[STOCK_SERVICE_MAX_SYMBOLS];
static stock_quote_meta_t s_meta[STOCK_SERVICE_MAX_SYMBOLS];
static stock_price_history_t
    s_intraday_history[STOCK_SERVICE_MAX_SYMBOLS];
static char
    s_history_updated_at[STOCK_SERVICE_MAX_SYMBOLS]
                        [STOCK_UPDATED_AT_MAX_LEN];
static bool s_history_loaded[STOCK_SERVICE_MAX_SYMBOLS];
static char
    s_history_loaded_minute[STOCK_SERVICE_MAX_SYMBOLS][13];
static size_t s_history_cursor;
static char s_last_error[STOCK_ERROR_MAX_LEN];
static uint32_t s_refresh_count;
static uint32_t s_failure_count;
static uint64_t s_last_refresh_attempt_us;
static bool s_refresh_requested;
static bool s_refreshing;
static bool s_started;
static TaskHandle_t s_task;
static char s_response[STOCK_SERVICE_RESPONSE_MAX];
static char s_history_response[STOCK_SERVICE_HISTORY_RESPONSE_MAX];
static stock_intraday_sample_t
    s_intraday_seed_samples[STOCK_SERVICE_INTRADAY_SEED_MAX];
static stock_market_watch_t s_market_watch[STOCK_SERVICE_MAX_SYMBOLS];
static stock_analysis_snapshot_t
    s_analysis[STOCK_SERVICE_MAX_SYMBOLS];
static uint32_t s_config_generation = 1u;
static uint32_t s_analysis_generation;

static uint64_t now_us(void)
{
    return (uint64_t)esp_timer_get_time();
}

static void copy_text(char *destination, size_t size, const char *source)
{
    if (destination == NULL || size == 0u) {
        return;
    }
    snprintf(destination, size, "%s", source == NULL ? "" : source);
}

static int64_t days_from_civil(int year, unsigned month, unsigned day)
{
    int era;
    unsigned adjusted_month;
    unsigned year_of_era;
    unsigned day_of_year;
    unsigned day_of_era;

    year -= month <= 2u;
    era = (year >= 0 ? year : year - 399) / 400;
    year_of_era = (unsigned)(year - era * 400);
    adjusted_month = month > 2u ? month - 3u : month + 9u;
    day_of_year = (153u * adjusted_month + 2u) / 5u + day - 1u;
    day_of_era = year_of_era * 365u + year_of_era / 4u -
                 year_of_era / 100u + day_of_year;
    return (int64_t)era * 146097 + (int64_t)day_of_era - 719468;
}

static uint32_t quote_timestamp_s(const stock_quote_t *quote)
{
    unsigned year;
    unsigned month;
    unsigned day;
    unsigned hour;
    unsigned minute;
    unsigned second;
    int64_t local_seconds;
    int64_t unix_seconds;
    time_t now;

    if (quote != NULL && strlen(quote->updated_at) == 14u &&
        sscanf(quote->updated_at, "%4u%2u%2u%2u%2u%2u",
               &year, &month, &day, &hour, &minute, &second) == 6 &&
        year >= 2024u && month >= 1u && month <= 12u &&
        day >= 1u && day <= 31u && hour <= 23u && minute <= 59u &&
        second <= 59u) {
        local_seconds =
            days_from_civil((int)year, month, day) * 86400 +
            (int64_t)hour * 3600 + (int64_t)minute * 60 +
            (int64_t)second;
        unix_seconds = local_seconds - 8 * 3600;
        if (unix_seconds >= (int64_t)STOCK_ANALYSIS_VALID_EPOCH &&
            unix_seconds <= (int64_t)UINT32_MAX) {
            return (uint32_t)unix_seconds;
        }
    }

    now = time(NULL);
    if ((int64_t)now >= (int64_t)STOCK_ANALYSIS_VALID_EPOCH &&
        (int64_t)now <= (int64_t)UINT32_MAX) {
        return (uint32_t)now;
    }
    return 0u;
}

static bool analysis_symbol_code(const char *symbol, char code[7])
{
    if (symbol == NULL || code == NULL || strlen(symbol) != 8u ||
        (strncmp(symbol, "sh", 2u) != 0 &&
         strncmp(symbol, "sz", 2u) != 0 &&
         strncmp(symbol, "bj", 2u) != 0)) {
        return false;
    }
    memcpy(code, symbol + 2u, STOCK_MARKET_SYMBOL_LEN);
    code[STOCK_MARKET_SYMBOL_LEN] = '\0';
    return true;
}

static uint32_t china_day(uint32_t timestamp_s)
{
    return (uint32_t)(((uint64_t)timestamp_s + 8u * 3600u) / 86400u);
}

static bool seed_analysis_from_intraday(
    size_t index, const char *symbol, uint32_t day_start_utc_s,
    const stock_intraday_sample_t *samples, size_t sample_count)
{
    stock_market_watch_t watch;
    stock_analysis_snapshot_t snapshot = {
        .supported = true,
        .valid = true,
        .has_closed = true,
    };
    stock_analyzer_result_t result;
    const stock_intraday_sample_t *last;
    char code[STOCK_MARKET_SYMBOL_LEN + 1u];
    uint32_t last_timestamp_s;

    if (index >= STOCK_SERVICE_MAX_SYMBOLS || symbol == NULL ||
        samples == NULL || sample_count < 2u ||
        !analysis_symbol_code(symbol, code)) {
        return false;
    }
    if (stock_market_init(&watch, code, NULL) != STOCK_MARKET_OK ||
        !stock_intraday_seed_analyzer(&watch.analyzer, samples, sample_count,
                                      day_start_utc_s, &result)) {
        return false;
    }

    last = &samples[sample_count - 1u];
    last_timestamp_s =
        day_start_utc_s + (uint32_t)last->minute_of_day * 60u;
    if (!isfinite(last->cumulative_volume) ||
        last->cumulative_volume < 0.0 ||
        last->cumulative_volume > (double)UINT64_MAX ||
        !isfinite(last->price) || last->price <= 0.0f) {
        return false;
    }

    watch.has_quote = 1u;
    watch.last_timestamp_s = last_timestamp_s;
    watch.day_key = china_day(last_timestamp_s);
    watch.last_volume_shares =
        (uint64_t)(last->cumulative_volume + 0.5);
    watch.last_price = last->price;
    snapshot.live = result;
    snapshot.closed = result;

    taskENTER_CRITICAL(&s_lock);
    s_market_watch[index] = watch;
    s_analysis[index] = snapshot;
    taskEXIT_CRITICAL(&s_lock);
    return true;
}

static void initialize_analysis(const stock_service_config_t *config)
{
    stock_analysis_snapshot_t snapshots[STOCK_SERVICE_MAX_SYMBOLS] = {0};

    memset(s_market_watch, 0, sizeof(s_market_watch));
    for (size_t index = 0u; index < config->symbol_count; index++) {
        char code[STOCK_MARKET_SYMBOL_LEN + 1u];

        if (!analysis_symbol_code(config->symbols[index], code)) {
            continue;
        }
        if (stock_market_init(&s_market_watch[index], code, NULL) ==
            STOCK_MARKET_OK) {
            snapshots[index].supported = true;
        }
    }

    taskENTER_CRITICAL(&s_lock);
    memcpy(s_analysis, snapshots, sizeof(s_analysis));
    taskEXIT_CRITICAL(&s_lock);
}

static void feed_analysis(size_t index, const stock_quote_t *quote)
{
    char code[STOCK_MARKET_SYMBOL_LEN + 1u];
    double cumulative_shares;
    stock_market_quote_t market_quote = {0};
    stock_market_update_t update = {0};
    stock_analysis_snapshot_t previous = {0};
    stock_analysis_snapshot_t snapshot = {0};
    stock_market_status_t status;
    uint32_t timestamp_s;

    if (index >= STOCK_SERVICE_MAX_SYMBOLS || quote == NULL ||
        !analysis_symbol_code(quote->symbol, code)) {
        return;
    }
    cumulative_shares = quote->volume_lots * 100.0;
    timestamp_s = quote_timestamp_s(quote);
    if (!isfinite(cumulative_shares) || cumulative_shares < 0.0 ||
        cumulative_shares > (double)UINT64_MAX || timestamp_s == 0u) {
        return;
    }

    memcpy(market_quote.symbol, code, sizeof(market_quote.symbol));
    market_quote.timestamp_s = timestamp_s;
    market_quote.last_price = (float)quote->price;
    market_quote.cumulative_volume_shares =
        (uint64_t)(cumulative_shares + 0.5);
    status = stock_market_feed(&s_market_watch[index], &market_quote,
                               &update);
    if (status < 0) {
        ESP_LOGW(TAG, "analysis input rejected for %s: %d", quote->symbol,
                 (int)status);
        return;
    }
    if (status == STOCK_MARKET_DUPLICATE) {
        return;
    }

    taskENTER_CRITICAL(&s_lock);
    previous = s_analysis[index];
    taskEXIT_CRITICAL(&s_lock);

    snapshot.supported = true;
    snapshot.valid = true;
    snapshot.has_closed = update.has_closed != 0u;
    snapshot.live = update.live;
    if (snapshot.has_closed) {
        snapshot.closed = update.closed;
    } else if (previous.has_closed) {
        snapshot.has_closed = true;
        snapshot.closed = previous.closed;
    }
    taskENTER_CRITICAL(&s_lock);
    s_analysis[index] = snapshot;
    taskEXIT_CRITICAL(&s_lock);
}

static bool feedback_mode_valid(stock_feedback_mode_t mode)
{
    return mode >= STOCK_FEEDBACK_BOTH && mode <= STOCK_FEEDBACK_SILENT;
}

static bool config_symbols_equal(const stock_service_config_t *left,
                                 const stock_service_config_t *right)
{
    if (left == NULL || right == NULL ||
        left->symbol_count != right->symbol_count) {
        return false;
    }
    for (size_t index = 0u; index < left->symbol_count; index++) {
        if (strcmp(left->symbols[index], right->symbols[index]) != 0) {
            return false;
        }
    }
    return true;
}

static bool config_valid(const stock_service_config_t *config)
{
    char normalized[STOCK_SERVICE_MAX_SYMBOLS]
                   [STOCK_SYMBOL_MAX_LEN + 1u] = {{0}};

    if (config == NULL || config->symbol_count == 0u ||
        config->symbol_count > STOCK_SERVICE_MAX_SYMBOLS ||
        config->refresh_seconds < STOCK_REFRESH_MIN_SECONDS ||
        config->refresh_seconds > STOCK_REFRESH_MAX_SECONDS ||
        !feedback_mode_valid(config->feedback_mode)) {
        return false;
    }

    for (size_t index = 0u; index < config->symbol_count; index++) {
        if (config->alert_upper_price_units[index] >
                STOCK_ALERT_PRICE_MAX_UNITS ||
            config->alert_lower_price_units[index] >
                STOCK_ALERT_PRICE_MAX_UNITS ||
            (config->alert_upper_price_units[index] != 0u &&
             config->alert_lower_price_units[index] != 0u &&
             config->alert_upper_price_units[index] <=
                 config->alert_lower_price_units[index])) {
            return false;
        }
        if (!stock_symbol_normalize(config->symbols[index],
                                    normalized[index])) {
            return false;
        }
        for (size_t other = 0u; other < index; other++) {
            if (strcmp(normalized[index], normalized[other]) == 0) {
                return false;
            }
        }
    }
    return true;
}

static void set_default_config(stock_service_config_t *config)
{
    memset(config, 0, sizeof(*config));
    config->alerts_enabled = true;
    config->symbol_count =
        (uint8_t)(sizeof(s_default_symbols) / sizeof(s_default_symbols[0]));
    config->refresh_seconds = STOCK_SERVICE_DEFAULT_REFRESH_SECONDS;
    config->feedback_mode = STOCK_FEEDBACK_BOTH;
    for (size_t index = 0u; index < config->symbol_count; index++) {
        copy_text(config->symbols[index], sizeof(config->symbols[index]),
                  s_default_symbols[index]);
    }
}

static bool quote_date_string(const stock_quote_t *quote, char *date)
{
    if (quote == NULL || date == NULL || strlen(quote->updated_at) < 12u) {
        return false;
    }
    for (size_t index = 0u; index < 12u; index++) {
        if (quote->updated_at[index] < '0' ||
            quote->updated_at[index] > '9') {
            return false;
        }
    }
    memcpy(date, quote->updated_at, 8u);
    date[8] = '\0';
    return true;
}

static bool normalize_config(stock_service_config_t *config)
{
    stock_service_config_t normalized;

    if (config == NULL || config->symbol_count == 0u ||
        config->symbol_count > STOCK_SERVICE_MAX_SYMBOLS) {
        return false;
    }
    normalized = *config;
    memset(normalized.symbols, 0, sizeof(normalized.symbols));
    for (size_t index = 0u; index < config->symbol_count; index++) {
        if (!stock_symbol_normalize(config->symbols[index],
                                    normalized.symbols[index])) {
            return false;
        }
    }
    if (!config_valid(&normalized)) {
        return false;
    }
    *config = normalized;
    return true;
}

static void append_intraday_history_locked(size_t index,
                                           const stock_quote_t *quote)
{
    stock_price_history_t *history;
    char *last_updated_at;
    bool has_timestamp;
    bool same_day;
    bool same_timestamp;

    if (index >= STOCK_SERVICE_MAX_SYMBOLS || quote == NULL ||
        !quote->valid || !isfinite(quote->price) || quote->price <= 0.0) {
        return;
    }
    history = &s_intraday_history[index];
    last_updated_at = s_history_updated_at[index];
    has_timestamp = quote->updated_at[0] != '\0';
    same_day = !has_timestamp ||
               strncmp(last_updated_at, quote->updated_at, 8u) == 0;
    same_timestamp = has_timestamp &&
                     strcmp(last_updated_at, quote->updated_at) == 0;

    if (!same_day) {
        history->count = 0u;
        s_history_loaded[index] = false;
        s_history_loaded_minute[index][0] = '\0';
    }
    if (same_timestamp && history->count > 0u) {
        history->prices[history->count - 1u] = (float)quote->price;
    } else {
        if (history->count >= STOCK_PRICE_HISTORY_MAX) {
            memmove(&history->prices[0], &history->prices[1],
                    sizeof(history->prices[0]) *
                        (STOCK_PRICE_HISTORY_MAX - 1u));
            history->count = STOCK_PRICE_HISTORY_MAX - 1u;
        }
        history->prices[history->count++] = (float)quote->price;
    }
    copy_text(last_updated_at, STOCK_UPDATED_AT_MAX_LEN,
              quote->updated_at);
}

static void reset_quotes_locked(void)
{
    memset(s_quotes, 0, sizeof(s_quotes));
    memset(s_meta, 0, sizeof(s_meta));
    memset(s_intraday_history, 0, sizeof(s_intraday_history));
    memset(s_history_updated_at, 0, sizeof(s_history_updated_at));
    memset(s_history_loaded, 0, sizeof(s_history_loaded));
    memset(s_history_loaded_minute, 0, sizeof(s_history_loaded_minute));
    s_history_cursor = 0u;
    for (size_t index = 0u; index < s_config.symbol_count; index++) {
        copy_text(s_quotes[index].symbol, sizeof(s_quotes[index].symbol),
                  s_config.symbols[index]);
    }
}

static bool load_config(stock_service_config_t *config)
{
    stock_persisted_config_buffer_t persisted = {0};
    size_t size = sizeof(persisted);
    bool found = false;
    esp_err_t err = config_service_load_blob(
        CONFIG_SERVICE_ITEM_STOCK_SETTINGS, &persisted, &size, &found);
    uint16_t version;

    if (err != ESP_OK || !found) {
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "saved stock settings unavailable: %s",
                     esp_err_to_name(err));
        }
        return false;
    }
    version = persisted.current.version;
    if (persisted.current.magic != STOCK_SERVICE_CONFIG_MAGIC ||
        (version != 1u && version != 2u &&
         version != STOCK_SERVICE_CONFIG_VERSION)) {
        ESP_LOGW(TAG, "saved stock settings are invalid; using defaults");
        return false;
    }

    memset(config, 0, sizeof(*config));
    if (version == STOCK_SERVICE_CONFIG_VERSION) {
        if (size != sizeof(stock_persisted_config_t)) {
            ESP_LOGW(TAG, "saved stock settings have an invalid size");
            return false;
        }
        config->alerts_enabled = persisted.current.alerts_enabled != 0u;
        config->symbol_count = persisted.current.symbol_count;
        config->refresh_seconds = persisted.current.refresh_seconds;
        config->feedback_mode =
            (stock_feedback_mode_t)persisted.current.feedback_mode;
        memcpy(config->alert_upper_price_units,
               persisted.current.alert_upper_price_units,
               sizeof(config->alert_upper_price_units));
        memcpy(config->alert_lower_price_units,
               persisted.current.alert_lower_price_units,
               sizeof(config->alert_lower_price_units));
        memcpy(config->symbols, persisted.current.symbols,
               sizeof(config->symbols));
    } else {
        if (size != sizeof(stock_persisted_config_legacy_t)) {
            ESP_LOGW(TAG, "saved stock settings have an invalid legacy size");
            return false;
        }
        config->alerts_enabled = persisted.legacy.alerts_enabled != 0u;
        config->symbol_count = persisted.legacy.symbol_count;
        config->refresh_seconds = persisted.legacy.refresh_seconds;
        config->feedback_mode =
            (stock_feedback_mode_t)persisted.legacy.feedback_mode;
        memcpy(config->symbols, persisted.legacy.symbols,
               sizeof(config->symbols));
        ESP_LOGI(TAG,
                 "migrated stock settings v%u; price limits start disabled",
                 (unsigned)version);
    }
    for (size_t index = 0u; index < STOCK_SERVICE_MAX_SYMBOLS; index++) {
        config->symbols[index][STOCK_SYMBOL_MAX_LEN] = '\0';
    }
    return normalize_config(config);
}

static esp_err_t save_config(const stock_service_config_t *config)
{
    stock_persisted_config_t persisted = {
        .magic = STOCK_SERVICE_CONFIG_MAGIC,
        .version = STOCK_SERVICE_CONFIG_VERSION,
        .refresh_seconds = config->refresh_seconds,
        .symbol_count = config->symbol_count,
        .feedback_mode = (uint8_t)config->feedback_mode,
        .alerts_enabled = config->alerts_enabled ? 1u : 0u,
    };

    memcpy(persisted.alert_upper_price_units,
           config->alert_upper_price_units,
           sizeof(persisted.alert_upper_price_units));
    memcpy(persisted.alert_lower_price_units,
           config->alert_lower_price_units,
           sizeof(persisted.alert_lower_price_units));
    memcpy(persisted.symbols, config->symbols, sizeof(persisted.symbols));
    return config_service_save_blob(
        CONFIG_SERVICE_ITEM_STOCK_SETTINGS, &persisted, sizeof(persisted));
}

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    http_response_buffer_t *buffer = event->user_data;

    if (event->event_id == HTTP_EVENT_ON_DATA && event->data != NULL &&
        event->data_len > 0 && buffer != NULL) {
        size_t available;
        size_t copy_length;

        if (buffer->length >= buffer->capacity - 1u) {
            buffer->truncated = true;
            return ESP_OK;
        }
        available = buffer->capacity - buffer->length - 1u;
        copy_length = (size_t)event->data_len;
        if (copy_length > available) {
            copy_length = available;
            buffer->truncated = true;
        }
        memcpy(buffer->data + buffer->length, event->data, copy_length);
        buffer->length += copy_length;
        buffer->data[buffer->length] = '\0';
    }
    return ESP_OK;
}

static esp_err_t request_url(const char *url, const char *host, char *response,
                             size_t response_size)
{
    http_response_buffer_t buffer = {
        .data = response,
        .capacity = response_size,
    };
    esp_http_client_config_t http_config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = STOCK_SERVICE_HTTP_TIMEOUT_MS,
        .event_handler = http_event_handler,
        .user_data = &buffer,
        .buffer_size = 1024,
        .common_name = host,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client;
    esp_err_t err;
    int status;

    if (url == NULL || host == NULL || response == NULL ||
        response_size < 2u) {
        return ESP_ERR_INVALID_ARG;
    }
    response[0] = '\0';
    client = esp_http_client_init(&http_config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }
    (void)esp_http_client_set_header(client, "User-Agent",
                                     "WeMosR32-StockMonitor/1.0");
    (void)esp_http_client_set_header(client, "Host", host);
    err = esp_http_client_perform(client);
    status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        return err;
    }
    if (status != 200) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (buffer.length == 0u) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (buffer.truncated) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static esp_err_t fetch_response(const stock_service_config_t *config,
                                char *response, size_t response_size)
{
    char path[STOCK_SERVICE_URL_MAX] = "q=";
    char url[STOCK_SERVICE_URL_MAX];
    esp_err_t primary_err;

    if (response == NULL || response_size < 2u) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t index = 0u; index < config->symbol_count; index++) {
        size_t used = strlen(path);
        size_t symbol_length = strlen(config->symbols[index]);

        if (used + symbol_length + 2u >= sizeof(path)) {
            return ESP_ERR_INVALID_SIZE;
        }
        if (index != 0u) {
            path[used++] = ',';
            path[used] = '\0';
        }
        memcpy(path + used, config->symbols[index], symbol_length + 1u);
    }

    if (snprintf(url, sizeof(url), "%s%s", STOCK_SERVICE_PRIMARY_URL, path) >=
        (int)sizeof(url)) {
        return ESP_ERR_INVALID_SIZE;
    }
    primary_err =
        request_url(url, STOCK_SERVICE_HOST, response, response_size);
    if (primary_err == ESP_OK) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "quote host request failed (%s), retrying via fallback IP",
             esp_err_to_name(primary_err));
    if (snprintf(url, sizeof(url), "%s%s", STOCK_SERVICE_FALLBACK_URL, path) >=
        (int)sizeof(url)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return request_url(url, STOCK_SERVICE_HOST, response, response_size);
}

static void keep_latest_intraday_sample(
    stock_intraday_sample_t *samples, size_t capacity, size_t *sample_count,
    const stock_intraday_sample_t *sample)
{
    if (samples == NULL || sample_count == NULL || sample == NULL ||
        capacity == 0u) {
        return;
    }
    if (*sample_count < capacity) {
        samples[(*sample_count)++] = *sample;
    } else {
        memmove(&samples[0], &samples[1],
                (capacity - 1u) * sizeof(samples[0]));
        samples[capacity - 1u] = *sample;
    }
}

static bool parse_intraday_response(
    const char *response, const char *symbol, const char *expected_date,
    stock_price_history_t *history, stock_intraday_sample_t *seed_samples,
    size_t seed_capacity, size_t *seed_count, uint32_t *day_start_utc_s)
{
    float chart_samples[STOCK_SERVICE_INTRADAY_SOURCE_MAX];
    size_t sample_count = 0u;
    cJSON *root;
    cJSON *code;
    cJSON *data;
    cJSON *symbol_data;
    cJSON *series;
    cJSON *date;
    cJSON *rows;
    bool parsed = false;

    if (response == NULL || symbol == NULL || expected_date == NULL ||
        history == NULL || seed_samples == NULL || seed_count == NULL ||
        day_start_utc_s == NULL || seed_capacity == 0u) {
        return false;
    }
    *seed_count = 0u;
    root = cJSON_Parse(response);
    if (root == NULL) {
        return false;
    }
    code = cJSON_GetObjectItemCaseSensitive(root, "code");
    data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsNumber(code) || code->valueint != 0 ||
        !cJSON_IsObject(data)) {
        goto cleanup;
    }
    symbol_data = cJSON_GetObjectItemCaseSensitive(data, symbol);
    if (!cJSON_IsObject(symbol_data)) {
        goto cleanup;
    }
    series = cJSON_GetObjectItemCaseSensitive(symbol_data, "data");
    if (!cJSON_IsObject(series)) {
        goto cleanup;
    }
    date = cJSON_GetObjectItemCaseSensitive(series, "date");
    rows = cJSON_GetObjectItemCaseSensitive(series, "data");
    if (!cJSON_IsString(date) || date->valuestring == NULL ||
        strcmp(date->valuestring, expected_date) != 0 ||
        !cJSON_IsArray(rows)) {
        goto cleanup;
    }

    for (int index = 0; index < cJSON_GetArraySize(rows); index++) {
        cJSON *row = cJSON_GetArrayItem(rows, index);
        stock_intraday_sample_t sample;

        if (!cJSON_IsString(row) || row->valuestring == NULL ||
            !stock_intraday_parse_sample(row->valuestring, &sample)) {
            continue;
        }
        if (sample_count >= STOCK_SERVICE_INTRADAY_SOURCE_MAX) {
            goto cleanup;
        }
        chart_samples[sample_count++] = sample.price;
        keep_latest_intraday_sample(seed_samples, seed_capacity, seed_count,
                                    &sample);
    }
    if (sample_count == 0u ||
        !stock_intraday_day_start_utc(expected_date, day_start_utc_s)) {
        goto cleanup;
    }

    history->count = (uint8_t)stock_intraday_downsample(
        chart_samples, sample_count, history->prices,
        STOCK_PRICE_HISTORY_MAX);
    parsed = history->count > 0u;

cleanup:
    cJSON_Delete(root);
    return parsed;
}

static esp_err_t request_intraday_history(const char *symbol, char *response,
                                          size_t response_size)
{
    char url[STOCK_SERVICE_URL_MAX];

    if (symbol == NULL || response == NULL || response_size < 2u) {
        return ESP_ERR_INVALID_ARG;
    }
    if (snprintf(url, sizeof(url), "%s%s",
                 STOCK_SERVICE_INTRADAY_PRIMARY_URL, symbol) >=
        (int)sizeof(url)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return request_url(url, STOCK_SERVICE_INTRADAY_HOST, response,
                       response_size);
}

static bool select_intraday_history_request_locked(
    const stock_service_config_t *config, size_t *index, char *symbol,
    char *date)
{
    char candidate_date[9];

    if (config == NULL || index == NULL || symbol == NULL || date == NULL ||
        config->symbol_count == 0u) {
        return false;
    }

    for (size_t attempt = 0u; attempt < config->symbol_count; attempt++) {
        size_t candidate = s_history_cursor % config->symbol_count;

        if (s_history_loaded[candidate] ||
            !quote_date_string(&s_quotes[candidate], candidate_date)) {
            s_history_cursor =
                (candidate + 1u) % config->symbol_count;
            continue;
        }
        *index = candidate;
        s_history_cursor = (candidate + 1u) % config->symbol_count;
        copy_text(symbol, STOCK_SYMBOL_MAX_LEN + 1u,
                  config->symbols[candidate]);
        copy_text(date, 9u, candidate_date);
        return true;
    }

    for (size_t attempt = 0u; attempt < config->symbol_count; attempt++) {
        size_t candidate = s_history_cursor % config->symbol_count;

        if (!quote_date_string(&s_quotes[candidate], candidate_date) ||
            strncmp(s_history_loaded_minute[candidate],
                    s_quotes[candidate].updated_at, 12u) == 0) {
            s_history_cursor =
                (candidate + 1u) % config->symbol_count;
            continue;
        }
        *index = candidate;
        s_history_cursor = (candidate + 1u) % config->symbol_count;
        copy_text(symbol, STOCK_SYMBOL_MAX_LEN + 1u,
                  config->symbols[candidate]);
        copy_text(date, 9u, candidate_date);
        return true;
    }
    return false;
}

static esp_err_t refresh_one_intraday_history(
    const stock_service_config_t *config, uint32_t config_generation)
{
    stock_price_history_t history = {0};
    char symbol[STOCK_SYMBOL_MAX_LEN + 1u];
    char date[9];
    char current_date[9];
    size_t seed_count = 0u;
    uint32_t day_start_utc_s = 0u;
    bool seeded;
    size_t index;
    esp_err_t err;

    taskENTER_CRITICAL(&s_lock);
    if (!select_intraday_history_request_locked(config, &index, symbol,
                                                date)) {
        taskEXIT_CRITICAL(&s_lock);
        return ESP_ERR_NOT_FOUND;
    }
    taskEXIT_CRITICAL(&s_lock);

    err = request_intraday_history(symbol, s_history_response,
                                   sizeof(s_history_response));
    if (err == ESP_OK &&
        !parse_intraday_response(s_history_response, symbol, date,
                                 &history, s_intraday_seed_samples,
                                 STOCK_SERVICE_INTRADAY_SEED_MAX,
                                 &seed_count, &day_start_utc_s)) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "intraday history unavailable for %s: %s", symbol,
                 esp_err_to_name(err));
        return err;
    }

    taskENTER_CRITICAL(&s_lock);
    if (config_generation != s_config_generation ||
        index >= s_config.symbol_count ||
        strcmp(s_config.symbols[index], symbol) != 0 ||
        !quote_date_string(&s_quotes[index], current_date) ||
        strcmp(current_date, date) != 0) {
        taskEXIT_CRITICAL(&s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_intraday_history[index] = history;
    s_history_loaded[index] = true;
    memcpy(s_history_loaded_minute[index], s_quotes[index].updated_at, 12u);
    s_history_loaded_minute[index][12] = '\0';
    taskEXIT_CRITICAL(&s_lock);

    seeded = seed_analysis_from_intraday(
        index, symbol, day_start_utc_s, s_intraday_seed_samples, seed_count);
    if (seeded) {
        ESP_LOGI(TAG, "intraday analysis seeded for %s: %u closed bars",
                 symbol,
                 seed_count > 1u ? (unsigned)(seed_count - 2u) : 0u);
    } else {
        ESP_LOGW(TAG, "intraday analysis seed unavailable for %s", symbol);
    }

    ESP_LOGI(TAG, "intraday history loaded for %s: %u points", symbol,
             (unsigned)history.count);
    return ESP_OK;
}

static size_t update_quotes_from_response(
    const stock_service_config_t *config, const char *response)
{
    stock_quote_t parsed[STOCK_SERVICE_MAX_SYMBOLS] = {0};
    size_t parsed_count = 0u;
    size_t updated = 0u;
    uint64_t timestamp = now_us();

    if (!stock_quote_parser_parse(response, parsed,
                                  STOCK_SERVICE_MAX_SYMBOLS,
                                  &parsed_count)) {
        return 0u;
    }

    taskENTER_CRITICAL(&s_lock);
    for (size_t request_index = 0u;
         request_index < config->symbol_count; request_index++) {
        for (size_t parsed_index = 0u; parsed_index < parsed_count;
             parsed_index++) {
            if (strcmp(config->symbols[request_index],
                       parsed[parsed_index].symbol) != 0) {
                continue;
            }
            s_quotes[request_index] = parsed[parsed_index];
            append_intraday_history_locked(request_index,
                                           &parsed[parsed_index]);
            s_meta[request_index].valid = true;
            s_meta[request_index].updated_at_us = timestamp;
            updated++;
            break;
        }
    }
    taskEXIT_CRITICAL(&s_lock);

    for (size_t request_index = 0u;
         request_index < config->symbol_count; request_index++) {
        for (size_t parsed_index = 0u; parsed_index < parsed_count;
             parsed_index++) {
            if (strcmp(config->symbols[request_index],
                       parsed[parsed_index].symbol) == 0) {
                feed_analysis(request_index, &parsed[parsed_index]);
                break;
            }
        }
    }
    return updated;
}

static void refresh_quotes(void)
{
    stock_service_config_t config;
    uint32_t config_generation;
    char error[STOCK_ERROR_MAX_LEN] = "";
    esp_err_t err;

    taskENTER_CRITICAL(&s_lock);
    config = s_config;
    config_generation = s_config_generation;
    s_refreshing = true;
    s_refresh_requested = false;
    s_last_refresh_attempt_us = now_us();
    taskEXIT_CRITICAL(&s_lock);

    if (config_generation != s_analysis_generation) {
        initialize_analysis(&config);
        s_analysis_generation = config_generation;
    }

    if (!wifi_manager_sta_connected()) {
        copy_text(error, sizeof(error), "WIFI OFFLINE");
        err = ESP_ERR_INVALID_STATE;
    } else {
        err = fetch_response(&config, s_response, sizeof(s_response));
        if (err == ESP_OK &&
            update_quotes_from_response(&config, s_response) == 0u) {
            err = ESP_ERR_NOT_FOUND;
        }
        if (err == ESP_OK) {
            (void)refresh_one_intraday_history(&config,
                                               config_generation);
        }
    }

    taskENTER_CRITICAL(&s_lock);
    s_refreshing = false;
    if (err == ESP_OK) {
        s_refresh_count++;
        s_last_error[0] = '\0';
    } else {
        s_failure_count++;
        copy_text(s_last_error, sizeof(s_last_error),
                  error[0] == '\0' ? esp_err_to_name(err) : error);
    }
    taskEXIT_CRITICAL(&s_lock);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "market data refreshed");
    } else {
        ESP_LOGW(TAG, "market data refresh failed: %s",
                 error[0] == '\0' ? esp_err_to_name(err) : error);
    }
}

static void stock_service_task(void *argument)
{
    bool network_was_connected = false;

    (void)argument;

    while (true) {
        bool refresh;
        bool network_connected;
        uint64_t next_due_us;
        uint64_t current_us;

        network_connected = wifi_manager_sta_connected();
        taskENTER_CRITICAL(&s_lock);
        current_us = now_us();
        next_due_us = s_last_refresh_attempt_us +
                      (uint64_t)s_config.refresh_seconds * 1000000u;
        if (network_connected && !network_was_connected) {
            s_refresh_requested = true;
        }
        refresh = network_connected &&
                  (s_refresh_requested || current_us >= next_due_us);
        taskEXIT_CRITICAL(&s_lock);
        network_was_connected = network_connected;

        if (refresh) {
            refresh_quotes();
        } else {
            (void)ulTaskNotifyTake(pdTRUE,
                                   pdMS_TO_TICKS(STOCK_SERVICE_POLL_MS));
        }
    }
}

esp_err_t stock_service_start(void)
{
    stock_service_config_t loaded;
    bool loaded_from_nvs;
    esp_err_t err;

    if (s_started) {
        return ESP_OK;
    }

    loaded_from_nvs = config_service_is_initialized() &&
                      load_config(&loaded);
    if (!loaded_from_nvs) {
        set_default_config(&loaded);
        if (!config_service_is_initialized()) {
            ESP_LOGW(TAG,
                     "configuration storage unavailable; using in-memory "
                     "defaults");
        }
    }

    taskENTER_CRITICAL(&s_lock);
    s_config = loaded;
    s_config_generation++;
    s_last_error[0] = '\0';
    s_refresh_count = 0u;
    s_failure_count = 0u;
    s_last_refresh_attempt_us = 0u;
    s_refresh_requested = false;
    reset_quotes_locked();
    taskEXIT_CRITICAL(&s_lock);
    s_analysis_generation = 0u;

    if (!loaded_from_nvs && config_service_is_initialized()) {
        err = save_config(&loaded);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "default stock settings were not persisted: %s",
                     esp_err_to_name(err));
        }
    }

    if (xTaskCreate(stock_service_task, "stock_service",
                    STOCK_SERVICE_TASK_STACK, NULL,
                    STOCK_SERVICE_TASK_PRIORITY, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    ESP_LOGI(TAG, "started with %u symbols, refresh=%us",
             (unsigned)loaded.symbol_count,
             (unsigned)loaded.refresh_seconds);
    return ESP_OK;
}

esp_err_t stock_service_set_config(const stock_service_config_t *config)
{
    stock_service_config_t normalized;
    bool symbols_changed;
    esp_err_t err;

    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    normalized = *config;
    if (!normalize_config(&normalized)) {
        return ESP_ERR_INVALID_ARG;
    }

    err = save_config(&normalized);
    if (err != ESP_OK) {
        return err;
    }

    taskENTER_CRITICAL(&s_lock);
    symbols_changed = !config_symbols_equal(&s_config, &normalized);
    s_config = normalized;
    s_last_error[0] = '\0';
    if (symbols_changed) {
        s_config_generation++;
        s_refresh_requested = true;
        s_last_refresh_attempt_us = 0u;
        reset_quotes_locked();
    }
    taskEXIT_CRITICAL(&s_lock);
    if (s_task != NULL) {
        xTaskNotifyGive(s_task);
    }
    ESP_LOGI(TAG, "stock settings updated");
    return ESP_OK;
}

void stock_service_get_config(stock_service_config_t *config)
{
    if (config == NULL) {
        return;
    }
    taskENTER_CRITICAL(&s_lock);
    *config = s_config;
    taskEXIT_CRITICAL(&s_lock);
}

void stock_service_get_snapshot(stock_service_snapshot_t *snapshot)
{
    uint64_t current_us;
    bool network_connected;

    if (snapshot == NULL) {
        return;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    current_us = now_us();
    network_connected = wifi_manager_sta_connected();
    taskENTER_CRITICAL(&s_lock);
    snapshot->ready = s_started;
    snapshot->network_connected = network_connected;
    snapshot->refreshing = s_refreshing;
    snapshot->symbol_count = s_config.symbol_count;
    snapshot->refresh_seconds = s_config.refresh_seconds;
    snapshot->refresh_count = s_refresh_count;
    snapshot->failure_count = s_failure_count;
    copy_text(snapshot->last_error, sizeof(snapshot->last_error),
              s_last_error);
    memcpy(snapshot->quotes, s_quotes, sizeof(snapshot->quotes));
    memcpy(snapshot->intraday_history, s_intraday_history,
           sizeof(snapshot->intraday_history));
    memcpy(snapshot->analysis, s_analysis, sizeof(snapshot->analysis));
    for (size_t index = 0u; index < STOCK_SERVICE_MAX_SYMBOLS; index++) {
        if (s_analysis[index].valid) {
            snapshot->analysis_ready_count++;
        }
    }
    for (size_t index = 0u; index < STOCK_SERVICE_MAX_SYMBOLS; index++) {
        if (!s_meta[index].valid) {
            continue;
        }
        snapshot->any_valid = true;
        if (s_meta[index].updated_at_us != 0u) {
            uint64_t age_us = current_us - s_meta[index].updated_at_us;
            uint32_t age_seconds =
                (uint32_t)(age_us / 1000000u);
            if (age_seconds > snapshot->seconds_since_update) {
                snapshot->seconds_since_update = age_seconds;
            }
        }
    }
    taskEXIT_CRITICAL(&s_lock);
}

esp_err_t stock_service_refresh_now(void)
{
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    taskENTER_CRITICAL(&s_lock);
    s_refresh_requested = true;
    taskEXIT_CRITICAL(&s_lock);
    if (s_task != NULL) {
        xTaskNotifyGive(s_task);
    }
    return ESP_OK;
}

bool stock_service_is_started(void)
{
    return s_started;
}
