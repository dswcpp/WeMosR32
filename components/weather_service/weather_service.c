#include "weather_service.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "config_service.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "time_service.h"
#include "wifi_manager.h"

#define WEATHER_SERVICE_DEFAULT_LOCATION "杭州"
#define WEATHER_SERVICE_DEFAULT_LATITUDE 30.2741
#define WEATHER_SERVICE_DEFAULT_LONGITUDE 120.1551

#define WEATHER_SERVICE_REFRESH_INTERVAL_US (30LL * 60LL * 1000000LL)
#define WEATHER_SERVICE_RETRY_INTERVAL_US (60LL * 1000000LL)
#define WEATHER_SERVICE_WAIT_MS 5000u
#define WEATHER_SERVICE_HTTP_TIMEOUT_MS 12000
#define WEATHER_SERVICE_RESPONSE_MAX 6144u
#define WEATHER_SERVICE_TASK_STACK 10240u
#define WEATHER_SERVICE_TASK_PRIORITY 3u

#define WEATHER_SETTINGS_RECORD_MAGIC 0x57535431u
#define WEATHER_CACHE_RECORD_MAGIC 0x57534331u
#define WEATHER_SETTINGS_RECORD_VERSION 1u
#define WEATHER_CACHE_RECORD_VERSION 2u

static const char *TAG = "weather_service";

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t enabled;
    uint8_t reserved;
    char location[WEATHER_SERVICE_LOCATION_MAX_LEN + 1];
    double latitude;
    double longitude;
} weather_settings_record_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t current_valid;
    uint8_t daily_valid;
    uint8_t is_day;
    uint8_t reserved[3];
    int16_t humidity_percent;
    int16_t weather_code;
    int16_t daily_weather_code;
    char observed_at[WEATHER_SERVICE_OBSERVED_AT_MAX_LEN + 1];
    double latitude;
    double longitude;
    double temperature_c;
    double apparent_temperature_c;
    double temperature_max_c;
    double temperature_min_c;
    int64_t fetched_at_unix_s;
} weather_cache_record_t;

typedef struct {
    char data[WEATHER_SERVICE_RESPONSE_MAX];
    size_t length;
    bool overflow;
} weather_http_buffer_t;

static bool s_initialized;
static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static weather_settings_record_t s_settings;
static weather_cache_record_t s_cache;
static bool s_cache_available;
static bool s_refreshing;
static bool s_force_refresh;
static int64_t s_last_attempt_us = -1;
static int64_t s_last_attempt_unix_s = -1;
static uint32_t s_refresh_count;
static uint32_t s_generation;
static char s_last_error[WEATHER_SERVICE_ERROR_MAX_LEN + 1];
static weather_http_buffer_t s_http_buffer;

static void lock_state(void)
{
    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void unlock_state(void)
{
    if (s_lock != NULL) {
        xSemaphoreGive(s_lock);
    }
}

static void copy_string(char *destination, size_t destination_size,
                        const char *source)
{
    if (destination == NULL || destination_size == 0) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    snprintf(destination, destination_size, "%s", source);
}

static char *trim_whitespace(char *text)
{
    char *end;

    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') {
        text++;
    }
    end = text + strlen(text);
    while (end > text &&
           (end[-1] == ' ' || end[-1] == '\t' ||
            end[-1] == '\r' || end[-1] == '\n')) {
        end--;
    }
    *end = '\0';
    return text;
}

static bool is_valid_settings(const weather_settings_record_t *settings)
{
    size_t location_length;

    if (settings == NULL || settings->magic != WEATHER_SETTINGS_RECORD_MAGIC ||
        settings->version != WEATHER_SETTINGS_RECORD_VERSION ||
        settings->enabled > 1u ||
        !isfinite(settings->latitude) || !isfinite(settings->longitude) ||
        settings->latitude < -90.0 || settings->latitude > 90.0 ||
        settings->longitude < -180.0 || settings->longitude > 180.0) {
        return false;
    }

    location_length =
        strnlen(settings->location, sizeof(settings->location));
    return location_length > 0 &&
           location_length <= WEATHER_SERVICE_LOCATION_MAX_LEN;
}

static bool is_valid_cache(const weather_cache_record_t *cache)
{
    if (cache == NULL || cache->magic != WEATHER_CACHE_RECORD_MAGIC ||
        cache->version != WEATHER_CACHE_RECORD_VERSION ||
        cache->current_valid > 1u || cache->daily_valid > 1u ||
        cache->is_day > 1u ||
        cache->humidity_percent < 0 || cache->humidity_percent > 100 ||
        cache->weather_code < 0 || cache->weather_code > 99 ||
        cache->daily_weather_code < 0 ||
        cache->daily_weather_code > 99 ||
        !isfinite(cache->latitude) || !isfinite(cache->longitude) ||
        cache->latitude < -90.0 || cache->latitude > 90.0 ||
        cache->longitude < -180.0 || cache->longitude > 180.0 ||
        !isfinite(cache->temperature_c) ||
        !isfinite(cache->apparent_temperature_c) ||
        !isfinite(cache->temperature_max_c) ||
        !isfinite(cache->temperature_min_c) ||
        cache->temperature_c < -100.0 || cache->temperature_c > 70.0 ||
        cache->apparent_temperature_c < -120.0 ||
        cache->apparent_temperature_c > 80.0 ||
        cache->temperature_max_c < -100.0 ||
        cache->temperature_max_c > 70.0 ||
        cache->temperature_min_c < -100.0 ||
        cache->temperature_min_c > 70.0 ||
        cache->temperature_min_c > cache->temperature_max_c) {
        return false;
    }

    return cache->observed_at[0] != '\0';
}

static bool cache_matches_settings(const weather_cache_record_t *cache)
{
    return fabs(cache->latitude - s_settings.latitude) < 0.0000001 &&
           fabs(cache->longitude - s_settings.longitude) < 0.0000001;
}

static void set_defaults(void)
{
    memset(&s_settings, 0, sizeof(s_settings));
    s_settings.magic = WEATHER_SETTINGS_RECORD_MAGIC;
    s_settings.version = WEATHER_SETTINGS_RECORD_VERSION;
    s_settings.enabled = 1u;
    copy_string(s_settings.location, sizeof(s_settings.location),
                WEATHER_SERVICE_DEFAULT_LOCATION);
    s_settings.latitude = WEATHER_SERVICE_DEFAULT_LATITUDE;
    s_settings.longitude = WEATHER_SERVICE_DEFAULT_LONGITUDE;
}

static void load_persisted_records(void)
{
    weather_settings_record_t saved_settings;
    weather_cache_record_t saved_cache;
    size_t size;
    bool found = false;
    esp_err_t err;

    size = sizeof(saved_settings);
    err = config_service_load_blob(CONFIG_SERVICE_ITEM_WEATHER_SETTINGS,
                                   &saved_settings, &size, &found);
    if (err == ESP_OK && found && size == sizeof(saved_settings) &&
        is_valid_settings(&saved_settings)) {
        s_settings = saved_settings;
        ESP_LOGI(TAG, "loaded weather settings for %s (%.4f, %.4f)",
                 s_settings.location, s_settings.latitude,
                 s_settings.longitude);
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "weather settings load failed: %s",
                 esp_err_to_name(err));
    }

    size = sizeof(saved_cache);
    err = config_service_load_blob(CONFIG_SERVICE_ITEM_WEATHER_CACHE,
                                   &saved_cache, &size, &found);
    if (err == ESP_OK && found && size == sizeof(saved_cache) &&
        is_valid_cache(&saved_cache) &&
        cache_matches_settings(&saved_cache)) {
        s_cache = saved_cache;
        s_cache_available = true;
        ESP_LOGI(TAG, "loaded cached weather from Unix time %lld",
                 (long long)s_cache.fetched_at_unix_s);
    } else if (err == ESP_OK && found && size == sizeof(saved_cache) &&
               is_valid_cache(&saved_cache)) {
        ESP_LOGW(TAG,
                 "discarding weather cache for a different location "
                 "(%.4f, %.4f)",
                 saved_cache.latitude, saved_cache.longitude);
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "weather cache load failed: %s",
                 esp_err_to_name(err));
    }
}

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    weather_http_buffer_t *buffer = event->user_data;

    if (event->event_id == HTTP_EVENT_ON_DATA &&
        event->data != NULL && event->data_len > 0 && buffer != NULL) {
        size_t incoming = (size_t)event->data_len;

        if (incoming >= sizeof(buffer->data) - buffer->length) {
            buffer->overflow = true;
            return ESP_FAIL;
        }
        memcpy(buffer->data + buffer->length, event->data, incoming);
        buffer->length += incoming;
        buffer->data[buffer->length] = '\0';
    }
    return ESP_OK;
}

static bool json_number(const cJSON *object, const char *name,
                        double *value)
{
    const cJSON *item;

    if (object == NULL || name == NULL || value == NULL) {
        return false;
    }
    item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) {
        return false;
    }
    *value = item->valuedouble;
    return true;
}

static bool json_integer(const cJSON *object, const char *name, int minimum,
                         int maximum, int *value)
{
    double number;

    if (!json_number(object, name, &number) ||
        number != (double)(int)number ||
        number < minimum || number > maximum) {
        return false;
    }
    *value = (int)number;
    return true;
}

static bool parse_weather_response(const char *json,
                                   int64_t fetched_at_unix_s,
                                   double latitude, double longitude,
                                   weather_cache_record_t *cache)
{
    cJSON *root;
    const cJSON *current;
    const cJSON *daily;
    const cJSON *current_time;
    const cJSON *weather_codes;
    const cJSON *temperature_max;
    const cJSON *temperature_min;
    const cJSON *daily_weather_code;
    const cJSON *daily_temperature_max;
    const cJSON *daily_temperature_min;
    weather_cache_record_t parsed = {
        .magic = WEATHER_CACHE_RECORD_MAGIC,
        .version = WEATHER_CACHE_RECORD_VERSION,
        .current_valid = 1u,
        .daily_valid = 1u,
        .latitude = latitude,
        .longitude = longitude,
        .fetched_at_unix_s = fetched_at_unix_s,
    };
    int is_day;
    int humidity_percent;
    int weather_code;

    root = cJSON_Parse(json);
    if (root == NULL) {
        ESP_LOGW(TAG, "weather response is not valid JSON");
        return false;
    }

    current = cJSON_GetObjectItemCaseSensitive(root, "current");
    daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    if (!cJSON_IsObject(current) || !cJSON_IsObject(daily)) {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "weather response has no current or daily object");
        return false;
    }
    current_time =
        cJSON_GetObjectItemCaseSensitive(current, "time");
    weather_codes =
        cJSON_GetObjectItemCaseSensitive(daily, "weather_code");
    temperature_max =
        cJSON_GetObjectItemCaseSensitive(daily, "temperature_2m_max");
    temperature_min =
        cJSON_GetObjectItemCaseSensitive(daily, "temperature_2m_min");
    daily_weather_code = cJSON_GetArrayItem(weather_codes, 0);
    daily_temperature_max = cJSON_GetArrayItem(temperature_max, 0);
    daily_temperature_min = cJSON_GetArrayItem(temperature_min, 0);

    if (!cJSON_IsString(current_time) ||
        current_time->valuestring == NULL ||
        current_time->valuestring[0] == '\0' ||
        !cJSON_IsArray(weather_codes) ||
        !cJSON_IsArray(temperature_max) ||
        !cJSON_IsArray(temperature_min) ||
        !cJSON_IsNumber(daily_weather_code) ||
        !cJSON_IsNumber(daily_temperature_max) ||
        !cJSON_IsNumber(daily_temperature_min) ||
        !json_number(current, "temperature_2m",
                     &parsed.temperature_c) ||
        !json_number(current, "apparent_temperature",
                     &parsed.apparent_temperature_c) ||
        !json_integer(current, "relative_humidity_2m", 0, 100,
                      &humidity_percent) ||
        !json_integer(current, "weather_code", 0, 99,
                      &weather_code) ||
        !json_integer(current, "is_day", 0, 1, &is_day)) {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "weather response is missing required fields");
        return false;
    }

    parsed.humidity_percent = (int16_t)humidity_percent;
    parsed.weather_code = (int16_t)weather_code;
    parsed.daily_weather_code =
        (int16_t)daily_weather_code->valueint;
    parsed.temperature_max_c = daily_temperature_max->valuedouble;
    parsed.temperature_min_c = daily_temperature_min->valuedouble;
    parsed.is_day = (uint8_t)is_day;
    copy_string(parsed.observed_at, sizeof(parsed.observed_at),
                current_time->valuestring);

    cJSON_Delete(root);
    if (!is_valid_cache(&parsed)) {
        ESP_LOGW(TAG, "weather response values are outside safe ranges");
        return false;
    }

    *cache = parsed;
    return true;
}

static esp_err_t fetch_weather(double latitude, double longitude,
                               weather_cache_record_t *cache)
{
    char url[384];
    esp_http_client_config_t config = {
        .method = HTTP_METHOD_GET,
        .timeout_ms = WEATHER_SERVICE_HTTP_TIMEOUT_MS,
        .user_agent = "WeMosR32-Weather/1.0",
        .event_handler = http_event_handler,
        .user_data = &s_http_buffer,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 1024,
        .buffer_size_tx = 512,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client;
    esp_err_t err;
    int status_code;
    int64_t fetched_at_unix_s;

    memset(&s_http_buffer, 0, sizeof(s_http_buffer));
    snprintf(
        url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast"
        "?latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m,relative_humidity_2m,"
        "apparent_temperature,weather_code,is_day"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min"
        "&timezone=Asia%%2FShanghai&forecast_days=1",
        latitude, longitude);
    config.url = url;

    client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    err = esp_http_client_set_header(client, "Accept", "application/json");
    if (err == ESP_OK) {
        err = esp_http_client_perform(client);
    }
    status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (s_http_buffer.overflow) {
        ESP_LOGW(TAG, "weather response exceeded %u bytes",
                 (unsigned)sizeof(s_http_buffer.data));
        return ESP_ERR_INVALID_SIZE;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "weather HTTPS request failed: %s",
                 esp_err_to_name(err));
        return err;
    }
    if (status_code != 200) {
        ESP_LOGW(TAG, "weather service returned HTTP %d", status_code);
        return ESP_ERR_INVALID_RESPONSE;
    }

    fetched_at_unix_s = (int64_t)time(NULL);
    if (!parse_weather_response(s_http_buffer.data, fetched_at_unix_s,
                                latitude, longitude, cache)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGI(TAG,
             "weather updated: %.1f C, feels %.1f C, humidity %d%%, code %d",
             cache->temperature_c, cache->apparent_temperature_c,
             cache->humidity_percent, cache->weather_code);
    return ESP_OK;
}

static void set_waiting_error(const char *message)
{
    lock_state();
    if (!s_refreshing && strcmp(s_last_error, message) != 0) {
        copy_string(s_last_error, sizeof(s_last_error), message);
    }
    unlock_state();
}

static void weather_task(void *argument)
{
    int64_t next_attempt_us = 0;

    (void)argument;

    while (true) {
        weather_settings_record_t settings;
        time_service_snapshot_t time_snapshot;
        int64_t current_us = esp_timer_get_time();
        int64_t attempt_us = -1;
        bool force_refresh;
        bool should_fetch;
        uint32_t generation = 0;
        esp_err_t err = ESP_OK;

        lock_state();
        settings = s_settings;
        force_refresh = s_force_refresh;
        s_force_refresh = false;
        unlock_state();

        if (!settings.enabled) {
            set_waiting_error("天气服务已关闭");
        } else if (!wifi_manager_sta_connected()) {
            set_waiting_error("等待上游 Wi-Fi 连接");
        } else {
            time_service_get_snapshot(&time_snapshot);
            if (!time_snapshot.valid) {
                set_waiting_error("等待设备完成网络校时");
            } else {
                should_fetch = force_refresh || current_us >= next_attempt_us;
                if (should_fetch) {
                    weather_cache_record_t cache;

                    lock_state();
                    s_refreshing = true;
                    generation = s_generation;
                    settings = s_settings;
                    unlock_state();

                    attempt_us = esp_timer_get_time();
                    err = fetch_weather(settings.latitude,
                                        settings.longitude, &cache);

                    lock_state();
                    if (generation == s_generation) {
                        s_refreshing = false;
                        s_last_attempt_us = attempt_us;
                        s_last_attempt_unix_s = (int64_t)time(NULL);
                        if (err == ESP_OK) {
                            esp_err_t save_err;

                            s_cache = cache;
                            s_cache_available = true;
                            s_last_error[0] = '\0';
                            s_refresh_count++;
                            save_err = config_service_save_blob(
                                CONFIG_SERVICE_ITEM_WEATHER_CACHE, &cache,
                                sizeof(cache));
                            if (save_err != ESP_OK) {
                                ESP_LOGW(TAG,
                                         "weather cache save failed: %s",
                                         esp_err_to_name(save_err));
                            }
                        } else {
                            snprintf(s_last_error, sizeof(s_last_error),
                                     "天气更新失败（%s）",
                                     esp_err_to_name(err));
                        }
                    } else {
                        err = ESP_ERR_INVALID_STATE;
                    }
                    unlock_state();
                    next_attempt_us = attempt_us +
                        (err == ESP_OK
                             ? WEATHER_SERVICE_REFRESH_INTERVAL_US
                             : WEATHER_SERVICE_RETRY_INTERVAL_US);
                }
            }
        }

        (void)ulTaskNotifyTake(pdTRUE,
                               pdMS_TO_TICKS(WEATHER_SERVICE_WAIT_MS));
    }
}

esp_err_t weather_service_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    if (!config_service_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    set_defaults();
    load_persisted_records();
    copy_string(s_last_error, sizeof(s_last_error),
                "等待本次开机后的首次天气更新");

    s_initialized = true;
    if (xTaskCreate(weather_task, "weather", WEATHER_SERVICE_TASK_STACK,
                    NULL, WEATHER_SERVICE_TASK_PRIORITY, &s_task) != pdPASS) {
        s_initialized = false;
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "weather service started for %s (%.4f, %.4f), source=Open-Meteo",
             s_settings.location, s_settings.latitude, s_settings.longitude);
    return ESP_OK;
}

bool weather_service_is_initialized(void)
{
    return s_initialized;
}

void weather_service_get_snapshot(weather_service_snapshot_t *snapshot)
{
    int64_t now_us;
    int64_t now_unix_s;

    if (snapshot == NULL) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    copy_string(snapshot->location, sizeof(snapshot->location),
                WEATHER_SERVICE_DEFAULT_LOCATION);
    snapshot->latitude = WEATHER_SERVICE_DEFAULT_LATITUDE;
    snapshot->longitude = WEATHER_SERVICE_DEFAULT_LONGITUDE;
    snapshot->last_attempt_age_s = -1;
    snapshot->fetched_age_s = -1;
    copy_string(snapshot->last_error, sizeof(snapshot->last_error),
                "天气服务尚未启动");

    if (!s_initialized) {
        return;
    }

    now_us = esp_timer_get_time();
    now_unix_s = (int64_t)time(NULL);

    lock_state();
    snapshot->enabled = s_settings.enabled != 0u;
    copy_string(snapshot->location, sizeof(snapshot->location),
                s_settings.location);
    snapshot->latitude = s_settings.latitude;
    snapshot->longitude = s_settings.longitude;
    snapshot->refreshing = s_refreshing;
    snapshot->refresh_count = s_refresh_count;
    copy_string(snapshot->last_error, sizeof(snapshot->last_error),
                s_last_error);

    if (s_cache_available) {
        snapshot->current_valid = s_cache.current_valid != 0u;
        snapshot->daily_valid = s_cache.daily_valid != 0u;
        snapshot->available =
            snapshot->current_valid && snapshot->daily_valid;
        copy_string(snapshot->observed_at, sizeof(snapshot->observed_at),
                    s_cache.observed_at);
        snapshot->temperature_c = s_cache.temperature_c;
        snapshot->apparent_temperature_c =
            s_cache.apparent_temperature_c;
        snapshot->humidity_percent = s_cache.humidity_percent;
        snapshot->weather_code = s_cache.weather_code;
        snapshot->is_day = s_cache.is_day != 0u;
        snapshot->daily_weather_code = s_cache.daily_weather_code;
        snapshot->temperature_max_c = s_cache.temperature_max_c;
        snapshot->temperature_min_c = s_cache.temperature_min_c;
        snapshot->fetched_at_unix_s = s_cache.fetched_at_unix_s;
        if (now_unix_s > 0 && s_cache.fetched_at_unix_s > 0 &&
            now_unix_s >= s_cache.fetched_at_unix_s) {
            snapshot->fetched_age_s =
                now_unix_s - s_cache.fetched_at_unix_s;
        }
    }
    if (s_last_attempt_us >= 0 && now_us >= s_last_attempt_us) {
        snapshot->last_attempt_age_s =
            (now_us - s_last_attempt_us) / 1000000LL;
    }
    snapshot->stale =
        !snapshot->available || s_last_error[0] != '\0' ||
        (snapshot->fetched_age_s >= 0 &&
         snapshot->fetched_age_s > 45 * 60);
    unlock_state();
}

esp_err_t weather_service_get_settings(
    weather_service_settings_t *settings)
{
    if (settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(settings, 0, sizeof(*settings));
    lock_state();
    settings->enabled = s_settings.enabled != 0u;
    copy_string(settings->location, sizeof(settings->location),
                s_settings.location);
    settings->latitude = s_settings.latitude;
    settings->longitude = s_settings.longitude;
    unlock_state();
    return ESP_OK;
}

esp_err_t weather_service_set_settings(
    const weather_service_settings_t *settings)
{
    weather_settings_record_t next = {
        .magic = WEATHER_SETTINGS_RECORD_MAGIC,
        .version = WEATHER_SETTINGS_RECORD_VERSION,
    };
    char location[WEATHER_SERVICE_LOCATION_MAX_LEN + 1];
    bool invalidate_cache;
    esp_err_t err;

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (settings == NULL || settings->location[0] == '\0' ||
        !isfinite(settings->latitude) || !isfinite(settings->longitude) ||
        settings->latitude < -90.0 || settings->latitude > 90.0 ||
        settings->longitude < -180.0 || settings->longitude > 180.0) {
        return ESP_ERR_INVALID_ARG;
    }

    copy_string(location, sizeof(location), settings->location);
    copy_string(next.location, sizeof(next.location),
                trim_whitespace(location));
    if (next.location[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    next.enabled = settings->enabled ? 1u : 0u;
    next.latitude = settings->latitude;
    next.longitude = settings->longitude;

    lock_state();
    invalidate_cache =
        s_settings.latitude != next.latitude ||
        s_settings.longitude != next.longitude;
    if (invalidate_cache) {
        err = config_service_save_blob_and_clear_blob(
            CONFIG_SERVICE_ITEM_WEATHER_SETTINGS, &next, sizeof(next),
            CONFIG_SERVICE_ITEM_WEATHER_CACHE);
    } else {
        err = config_service_save_blob(
            CONFIG_SERVICE_ITEM_WEATHER_SETTINGS, &next, sizeof(next));
    }
    if (err != ESP_OK) {
        unlock_state();
        return err;
    }

    s_settings = next;
    s_generation++;
    s_force_refresh = next.enabled != 0u;
    if (invalidate_cache) {
        memset(&s_cache, 0, sizeof(s_cache));
        s_cache_available = false;
    }
    copy_string(s_last_error, sizeof(s_last_error),
                next.enabled ? "天气位置已更新，等待刷新"
                             : "天气服务已关闭");
    unlock_state();

    if (s_task != NULL) {
        xTaskNotifyGive(s_task);
    }

    ESP_LOGI(TAG, "weather settings saved for %s (%.4f, %.4f), enabled=%d",
             next.location, next.latitude, next.longitude, next.enabled);
    return ESP_OK;
}

esp_err_t weather_service_request_refresh(void)
{
    bool enabled;

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    lock_state();
    enabled = s_settings.enabled != 0u;
    if (enabled) {
        s_force_refresh = true;
    }
    unlock_state();

    if (!enabled || s_task == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    xTaskNotifyGive(s_task);
    return ESP_OK;
}
