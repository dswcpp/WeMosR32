#include "time_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"

#define TIME_SERVICE_POSIX_TZ "CST-8"
#define TIME_SERVICE_UTC_OFFSET_S (8 * 60 * 60)
#define TIME_SERVICE_VALID_EPOCH 1704067200LL
#define TIME_SERVICE_RETRY_DELAY_MS 30000
#define TIME_SERVICE_REFRESH_DELAY_MS (60 * 60 * 1000)

static const char *TAG = "time_service";

extern const char time_date_ca_pem_start[]
    asm("_binary_time_date_ca_pem_start");

typedef enum {
    TIME_SYNC_SOURCE_NONE = 0,
    TIME_SYNC_SOURCE_SNTP,
    TIME_SYNC_SOURCE_HTTP_DATE,
    TIME_SYNC_SOURCE_PHONE,
} time_sync_source_t;

typedef struct {
    const char *url;
    const char *host;
    int expected_status;
    bool use_tls;
} http_time_endpoint_t;

static const http_time_endpoint_t s_http_time_endpoints[] = {
    {
        .url = "https://connect.rom.miui.com/generate_204",
        .host = "connect.rom.miui.com",
        .expected_status = 204,
        .use_tls = true,
    },
    {
        .url = "https://connectivitycheck.platform.hicloud.com/generate_204",
        .host = "connectivitycheck.platform.hicloud.com",
        .expected_status = 204,
        .use_tls = true,
    },
    {
        .url = "https://www.baidu.com/",
        .host = "www.baidu.com",
        .expected_status = 200,
        .use_tls = true,
    },
    {
        .url = "http://220.181.104.183/generate_204",
        .host = "connect.rom.miui.com",
        .expected_status = 204,
    },
    {
        .url = "http://202.69.4.32/generate_204",
        .host = "connect.rom.miui.com",
        .expected_status = 204,
    },
    {
        .url = "http://115.231.82.96/generate_204",
        .host = "connectivitycheck.platform.hicloud.com",
        .expected_status = 204,
    },
    {
        .url = "http://connect.rom.miui.com/generate_204",
        .host = "connect.rom.miui.com",
        .expected_status = 204,
    },
    {
        .url = "http://www.baidu.com/",
        .host = "www.baidu.com",
        .expected_status = 200,
    },
};

typedef struct {
    char value[48];
    bool found;
} http_date_capture_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_initialized;
static time_sync_source_t s_sync_source;
static const char *s_sync_endpoint = "ntp.aliyun.com";
static int64_t s_last_sync_uptime_us = -1;
static int64_t s_last_sync_unix_s;
static uint32_t s_sync_count;

static const char *sync_source_name(time_sync_source_t source)
{
    switch (source) {
    case TIME_SYNC_SOURCE_SNTP:
        return "sntp";
    case TIME_SYNC_SOURCE_HTTP_DATE:
        return "http-date";
    case TIME_SYNC_SOURCE_PHONE:
        return "phone";
    default:
        return "rtc";
    }
}

static void record_sync(int64_t unix_s, time_sync_source_t source,
                        const char *endpoint)
{
    int64_t now_us = esp_timer_get_time();

    portENTER_CRITICAL(&s_lock);
    s_sync_source = source;
    s_sync_endpoint = endpoint;
    s_last_sync_uptime_us = now_us;
    s_last_sync_unix_s = unix_s;
    s_sync_count++;
    portEXIT_CRITICAL(&s_lock);
}

static void sntp_sync_callback(struct timeval *tv)
{
    int64_t unix_s = tv == NULL ? 0 : (int64_t)tv->tv_sec;

    record_sync(unix_s, TIME_SYNC_SOURCE_SNTP, "ntp.aliyun.com");

    ESP_LOGI(TAG, "SNTP synchronized to Unix time %lld",
             (long long)unix_s);
}

static int month_index(const char *month)
{
    static const char *const months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };

    for (int index = 0; index < 12; index++) {
        if (strcasecmp(month, months[index]) == 0) {
            return index + 1;
        }
    }
    return 0;
}

static int64_t days_from_civil(int year, unsigned month, unsigned day)
{
    int era;
    unsigned adjusted_month;
    unsigned year_of_era;
    unsigned day_of_year;
    unsigned day_of_era;

    year -= month <= 2;
    era = (year >= 0 ? year : year - 399) / 400;
    year_of_era = (unsigned)(year - era * 400);
    adjusted_month = month > 2 ? month - 3 : month + 9;
    day_of_year = (153 * adjusted_month + 2) / 5 + day - 1;
    day_of_era =
        year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return (int64_t)era * 146097 + (int64_t)day_of_era - 719468;
}

static bool parse_http_date(const char *value, int64_t *unix_time)
{
    char weekday[4] = {0};
    char month_name[4] = {0};
    char zone[4] = {0};
    int day;
    int year;
    int hour;
    int minute;
    int second;
    int month;
    int64_t days;

    if (value == NULL || unix_time == NULL) {
        return false;
    }

    if (sscanf(value, "%3[A-Za-z], %d %3[A-Za-z] %d %d:%d:%d %3[A-Za-z]",
               weekday, &day, month_name, &year, &hour, &minute, &second,
               zone) != 8) {
        return false;
    }

    month = month_index(month_name);
    if (month == 0 || strcasecmp(zone, "GMT") != 0 ||
        year < 2024 || year > 9999 ||
        day < 1 || day > 31 ||
        hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 ||
        second < 0 || second > 60) {
        return false;
    }

    days = days_from_civil(year, (unsigned)month, (unsigned)day);
    *unix_time =
        days * 86400 + hour * 3600 + minute * 60 + second;
    return true;
}

static esp_err_t http_client_event_handler(esp_http_client_event_t *event)
{
    http_date_capture_t *capture = event->user_data;

    if (event->event_id == HTTP_EVENT_ON_HEADER &&
        capture != NULL &&
        event->header_key != NULL &&
        event->header_value != NULL &&
        strcasecmp(event->header_key, "Date") == 0) {
        snprintf(capture->value, sizeof(capture->value), "%s",
                 event->header_value);
        capture->found = true;
    }
    return ESP_OK;
}

static esp_err_t sync_from_http_date_endpoint(
    const http_time_endpoint_t *endpoint)
{
    http_date_capture_t capture = {0};
    esp_http_client_config_t config = {
        .url = endpoint->url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 4000,
        .user_agent =
            "Mozilla/5.0 (Linux; Android 10) AppleWebKit/537.36 Chrome/120",
        .disable_auto_redirect = true,
        .event_handler = http_client_event_handler,
        .user_data = &capture,
        .buffer_size = 1024,
    };
    esp_http_client_handle_t client;
    int64_t unix_time;
    int status_code;
    esp_err_t err;

    if (endpoint->use_tls) {
        config.cert_pem = time_date_ca_pem_start;
    }

    client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    err = esp_http_client_set_header(client, "Host", endpoint->host);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    err = esp_http_client_perform(client);
    status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HTTP time request failed: %s (%s)",
                 esp_err_to_name(err), endpoint->host);
        return err;
    }
    if (status_code != endpoint->expected_status || !capture.found ||
        !parse_http_date(capture.value, &unix_time)) {
        ESP_LOGW(TAG,
                 "invalid HTTP time response: host=%s, status=%d, expected=%d, "
                 "Date=%s",
                 endpoint->host, status_code, endpoint->expected_status,
                 capture.found ? capture.value : "missing");
        return ESP_ERR_INVALID_RESPONSE;
    }

    struct timeval tv = {
        .tv_sec = (time_t)unix_time,
        .tv_usec = 0,
    };
    if (settimeofday(&tv, NULL) != 0) {
        return ESP_FAIL;
    }

    record_sync(unix_time, TIME_SYNC_SOURCE_HTTP_DATE, endpoint->host);
    ESP_LOGI(TAG, "HTTP Date synchronized to Unix time %lld via %s",
             (long long)unix_time, endpoint->host);
    return ESP_OK;
}

static esp_err_t sync_from_http_date(void)
{
    esp_err_t last_err = ESP_FAIL;

    for (size_t index = 0;
         index < sizeof(s_http_time_endpoints) /
                     sizeof(s_http_time_endpoints[0]);
         index++) {
        last_err =
            sync_from_http_date_endpoint(&s_http_time_endpoints[index]);
        if (last_err == ESP_OK) {
            return ESP_OK;
        }
    }
    return last_err;
}

static void http_time_sync_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(10000));

    while (true) {
        time_service_snapshot_t snapshot;

        time_service_get_snapshot(&snapshot);
        if (strcmp(snapshot.source, "sntp") != 0) {
            sync_from_http_date();
            time_service_get_snapshot(&snapshot);
        }

        vTaskDelay(pdMS_TO_TICKS(snapshot.synced
                                     ? TIME_SERVICE_REFRESH_DELAY_MS
                                     : TIME_SERVICE_RETRY_DELAY_MS));
    }
}

esp_err_t time_service_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    if (setenv("TZ", TIME_SERVICE_POSIX_TZ, 1) != 0) {
        ESP_LOGE(TAG, "failed to configure local timezone");
        return ESP_FAIL;
    }
    tzset();

    esp_sntp_config_t config =
        ESP_NETIF_SNTP_DEFAULT_CONFIG("ntp.aliyun.com");
    config.wait_for_sync = false;
    config.sync_cb = sntp_sync_callback;
    config.renew_servers_after_new_IP = true;

    esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SNTP initialization failed: %s", esp_err_to_name(err));
        return err;
    }

    if (xTaskCreate(http_time_sync_task, "time_http", 8192, NULL, 3, NULL) !=
        pdPASS) {
        esp_netif_sntp_deinit();
        ESP_LOGE(TAG, "failed to create HTTP time fallback task");
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    ESP_LOGI(TAG,
             "time synchronization started (SNTP primary, HTTPS Date fallback)");
    return ESP_OK;
}

esp_err_t time_service_set_unix_time(int64_t unix_time)
{
    struct timeval tv;

    if (unix_time < TIME_SERVICE_VALID_EPOCH ||
        unix_time > 4102444800LL) {
        return ESP_ERR_INVALID_ARG;
    }

    tv.tv_sec = (time_t)unix_time;
    tv.tv_usec = 0;
    if (settimeofday(&tv, NULL) != 0) {
        return ESP_FAIL;
    }

    record_sync(unix_time, TIME_SYNC_SOURCE_PHONE, "phone");
    ESP_LOGI(TAG, "clock synchronized from phone: Unix time %lld",
             (long long)unix_time);
    return ESP_OK;
}

void time_service_get_snapshot(time_service_snapshot_t *snapshot)
{
    time_sync_source_t sync_source;
    const char *sync_endpoint;
    int64_t last_sync_uptime_us;
    int64_t last_sync_unix_s;
    uint32_t sync_count;
    time_t now;

    if (snapshot == NULL) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    now = time(NULL);

    portENTER_CRITICAL(&s_lock);
    sync_source = s_sync_source;
    sync_endpoint = s_sync_endpoint;
    last_sync_uptime_us = s_last_sync_uptime_us;
    last_sync_unix_s = s_last_sync_unix_s;
    sync_count = s_sync_count;
    portEXIT_CRITICAL(&s_lock);

    snapshot->unix_time = (int64_t)now;
    snapshot->valid = now != (time_t)-1 &&
                      snapshot->unix_time >= TIME_SERVICE_VALID_EPOCH;
    snapshot->synced = sync_source != TIME_SYNC_SOURCE_NONE &&
                       snapshot->valid;
    snapshot->source = snapshot->synced
                           ? sync_source_name(sync_source)
                           : snapshot->valid ? "rtc" : "unavailable";
    snapshot->timezone = "Asia/Shanghai";
    snapshot->ntp_server = "ntp.aliyun.com";
    snapshot->sync_endpoint = sync_endpoint;
    snapshot->utc_offset_s = TIME_SERVICE_UTC_OFFSET_S;
    snapshot->sync_count = sync_count;
    snapshot->last_sync_unix_s = last_sync_unix_s;
    snapshot->last_sync_uptime_s =
        last_sync_uptime_us < 0 ? -1 : last_sync_uptime_us / 1000000LL;
    if (last_sync_unix_s <= 0 || !snapshot->valid) {
        snapshot->last_sync_age_s = -1;
    } else {
        int64_t age = snapshot->unix_time - last_sync_unix_s;
        snapshot->last_sync_age_s = age < 0 ? 0 : age;
    }
}

esp_err_t time_service_format_iso8601(char *buffer, size_t buffer_size)
{
    time_service_snapshot_t snapshot;
    struct tm local_time;
    time_t value;
    int written;

    if (buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    time_service_get_snapshot(&snapshot);
    if (!snapshot.valid) {
        buffer[0] = '\0';
        return ESP_ERR_INVALID_STATE;
    }

    value = (time_t)snapshot.unix_time;
    if (localtime_r(&value, &local_time) == NULL) {
        buffer[0] = '\0';
        return ESP_FAIL;
    }

    written = snprintf(buffer, buffer_size,
                       "%04d-%02d-%02dT%02d:%02d:%02d+08:00",
                       local_time.tm_year + 1900, local_time.tm_mon + 1,
                       local_time.tm_mday, local_time.tm_hour,
                       local_time.tm_min, local_time.tm_sec);
    if (written < 0 || (size_t)written >= buffer_size) {
        buffer[0] = '\0';
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}
