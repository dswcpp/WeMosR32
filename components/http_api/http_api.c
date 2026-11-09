#include "http_api.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "care_guidance.h"
#include "cJSON.h"
#include "config_service.h"
#include "esp_flash_encrypt.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "rgb_indicator.h"
#include "startup_service.h"
#include "time_service.h"
#include "weather_service.h"
#include "wellness_monitor.h"
#include "wifi_manager.h"

static const char *TAG = "http_api";
static httpd_handle_t s_server;

#define HTTP_API_JSON_BODY_MAX 768
#define WEB_SETTINGS_BLOB_VERSION 1u

typedef enum {
    WEB_PAGE_CLOCK = 0,
    WEB_PAGE_ENVIRONMENT,
    WEB_PAGE_SEDENTARY,
    WEB_PAGE_WATER,
    WEB_PAGE_PRESENCE,
    WEB_PAGE_TODAY_CARE,
    WEB_PAGE_WEATHER,
    WEB_PAGE_COUNT,
} web_page_t;

typedef struct {
    uint8_t version;
    uint8_t page_mask;
} web_settings_blob_t;

#define WEB_PAGE_ALL_MASK \
    ((uint8_t)((1u << (uint32_t)WEB_PAGE_COUNT) - 1u))

static const char *const s_web_page_names[WEB_PAGE_COUNT] = {
    [WEB_PAGE_CLOCK] = "clock",
    [WEB_PAGE_ENVIRONMENT] = "environment",
    [WEB_PAGE_SEDENTARY] = "sedentary",
    [WEB_PAGE_WATER] = "water",
    [WEB_PAGE_PRESENCE] = "presence",
    [WEB_PAGE_TODAY_CARE] = "today_care",
    [WEB_PAGE_WEATHER] = "weather",
};

static uint8_t s_web_page_mask = WEB_PAGE_ALL_MASK;

extern const uint8_t index_html_gz_start[]
    asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[]
    asm("_binary_index_html_gz_end");
extern const uint8_t manifest_webmanifest_start[]
    asm("_binary_manifest_webmanifest_start");
extern const uint8_t manifest_webmanifest_end[]
    asm("_binary_manifest_webmanifest_end");
extern const uint8_t icon_svg_start[] asm("_binary_icon_svg_start");
extern const uint8_t icon_svg_end[] asm("_binary_icon_svg_end");

static bool is_valid_web_page_mask(uint8_t page_mask)
{
    return page_mask != 0u &&
           (page_mask & (uint8_t)~WEB_PAGE_ALL_MASK) == 0u;
}

static esp_err_t load_web_page_settings(void)
{
    web_settings_blob_t blob = {
        .version = WEB_SETTINGS_BLOB_VERSION,
        .page_mask = WEB_PAGE_ALL_MASK,
    };
    size_t size = sizeof(blob);
    bool found = false;
    esp_err_t err = config_service_load_blob(
        CONFIG_SERVICE_ITEM_WEB_SETTINGS, &blob, &size, &found);

    if (err == ESP_OK && found &&
        (size != sizeof(blob) ||
         blob.version != WEB_SETTINGS_BLOB_VERSION ||
         !is_valid_web_page_mask(blob.page_mask))) {
        ESP_LOGW(TAG, "saved web page settings are invalid; using defaults");
        err = ESP_ERR_INVALID_VERSION;
    }

    s_web_page_mask =
        err == ESP_OK && found ? blob.page_mask : WEB_PAGE_ALL_MASK;
    return err;
}

static esp_err_t save_web_page_settings(uint8_t page_mask)
{
    const web_settings_blob_t blob = {
        .version = WEB_SETTINGS_BLOB_VERSION,
        .page_mask = page_mask,
    };
    esp_err_t err;

    if (!is_valid_web_page_mask(page_mask)) {
        return ESP_ERR_INVALID_ARG;
    }
    err = config_service_save_blob(
        CONFIG_SERVICE_ITEM_WEB_SETTINGS, &blob, sizeof(blob));
    if (err == ESP_OK) {
        s_web_page_mask = page_mask;
    }
    return err;
}

static bool add_web_settings_object(cJSON *parent)
{
    cJSON *web;
    cJSON *pages;

    if (parent == NULL) {
        return false;
    }
    web = cJSON_AddObjectToObject(parent, "web");
    if (web == NULL) {
        return false;
    }
    pages = cJSON_AddObjectToObject(web, "pages");
    if (pages == NULL) {
        return false;
    }

    for (size_t page = 0u; page < (size_t)WEB_PAGE_COUNT; page++) {
        cJSON_AddBoolToObject(
            pages, s_web_page_names[page],
            (s_web_page_mask & (uint8_t)(1u << page)) != 0u);
    }
    return true;
}

static bool add_startup_object(cJSON *parent)
{
    startup_snapshot_t startup;
    cJSON *object;
    cJSON *services;

    if (parent == NULL) {
        return false;
    }

    startup_service_get_snapshot(&startup);
    object = cJSON_AddObjectToObject(parent, "startup");
    if (object == NULL) {
        return false;
    }

    cJSON_AddBoolToObject(object, "complete", startup.complete);
    cJSON_AddStringToObject(
        object, "state", startup_state_name(startup.state));
    cJSON_AddBoolToObject(object, "degraded", startup.degraded);
    cJSON_AddBoolToObject(object, "blocked", startup.blocked);
    cJSON_AddStringToObject(
        object, "stage", startup_service_stage_name(startup.stage));
    cJSON_AddStringToObject(
        object, "reset_reason",
        startup_reset_reason_name(startup.reset_reason));
    cJSON_AddNumberToObject(object, "started_at_ms",
                            (double)startup.started_ms);
    cJSON_AddNumberToObject(object, "finished_at_ms",
                            (double)startup.finished_ms);
    cJSON_AddNumberToObject(object, "duration_ms",
                            (double)startup.duration_ms);

    services = cJSON_AddObjectToObject(object, "services");
    if (services == NULL) {
        return false;
    }

    for (size_t index = 0u; index < STARTUP_SERVICE_COUNT; index++) {
        const startup_service_record_t *record =
            &startup.services[index];
        cJSON *service = cJSON_AddObjectToObject(
            services,
            startup_service_name((startup_service_id_t)index));

        if (service == NULL) {
            return false;
        }
        cJSON_AddStringToObject(
            service, "state",
            startup_service_state_name(record->state));
        cJSON_AddNumberToObject(service, "error",
                                (double)record->last_error);
        cJSON_AddStringToObject(
            service, "error_name",
            esp_err_to_name((esp_err_t)record->last_error));
        cJSON_AddNumberToObject(service, "started_at_ms",
                                (double)record->started_ms);
        cJSON_AddNumberToObject(service, "finished_at_ms",
                                (double)record->finished_ms);
        cJSON_AddNumberToObject(service, "duration_ms",
                                (double)record->duration_ms);
    }
    return true;
}

static bool fill_time_object(cJSON *time)
{
    time_service_snapshot_t time_snapshot;
    char iso8601[32];

    if (time == NULL) {
        return false;
    }

    time_service_get_snapshot(&time_snapshot);
    cJSON_AddBoolToObject(time, "valid", time_snapshot.valid);
    cJSON_AddBoolToObject(time, "synced", time_snapshot.synced);
    cJSON_AddStringToObject(time, "source", time_snapshot.source);
    cJSON_AddNumberToObject(time, "unix_time",
                            (double)time_snapshot.unix_time);
    if (time_service_format_iso8601(iso8601, sizeof(iso8601)) == ESP_OK) {
        cJSON_AddStringToObject(time, "iso8601", iso8601);
    } else {
        cJSON_AddNullToObject(time, "iso8601");
    }
    cJSON_AddStringToObject(time, "timezone", time_snapshot.timezone);
    cJSON_AddNumberToObject(time, "utc_offset_s",
                            (double)time_snapshot.utc_offset_s);
    cJSON_AddStringToObject(time, "ntp_server", time_snapshot.ntp_server);
    cJSON_AddStringToObject(time, "sync_endpoint",
                            time_snapshot.sync_endpoint);
    cJSON_AddNumberToObject(time, "last_sync_unix_time",
                            (double)time_snapshot.last_sync_unix_s);
    cJSON_AddNumberToObject(time, "last_sync_s",
                            (double)time_snapshot.last_sync_uptime_s);
    cJSON_AddNumberToObject(time, "last_sync_age_s",
                            (double)time_snapshot.last_sync_age_s);
    cJSON_AddNumberToObject(time, "sync_count", time_snapshot.sync_count);
    return true;
}

static bool add_time_object(cJSON *parent)
{
    return fill_time_object(cJSON_AddObjectToObject(parent, "time"));
}

static bool add_weather_object(cJSON *parent)
{
    weather_service_snapshot_t weather;
    cJSON *object;
    cJSON *current;
    cJSON *daily;

    if (parent == NULL) {
        return false;
    }

    weather_service_get_snapshot(&weather);
    object = cJSON_AddObjectToObject(parent, "weather");
    if (object == NULL) {
        return false;
    }
    current = cJSON_AddObjectToObject(object, "current");
    daily = cJSON_AddObjectToObject(object, "daily");
    if (current == NULL || daily == NULL) {
        return false;
    }

    cJSON_AddBoolToObject(object, "ready",
                          weather_service_is_initialized());
    cJSON_AddStringToObject(object, "source", "open-meteo");
    cJSON_AddBoolToObject(object, "enabled", weather.enabled);
    cJSON_AddStringToObject(object, "location", weather.location);
    cJSON_AddNumberToObject(object, "latitude", weather.latitude);
    cJSON_AddNumberToObject(object, "longitude", weather.longitude);
    cJSON_AddBoolToObject(object, "available", weather.available);
    cJSON_AddBoolToObject(object, "stale", weather.stale);
    cJSON_AddBoolToObject(object, "refreshing", weather.refreshing);
    cJSON_AddStringToObject(object, "observed_at", weather.observed_at);
    cJSON_AddNumberToObject(object, "fetched_at_unix_s",
                            (double)weather.fetched_at_unix_s);
    cJSON_AddNumberToObject(object, "fetched_age_s",
                            (double)weather.fetched_age_s);
    cJSON_AddNumberToObject(object, "last_attempt_age_s",
                            (double)weather.last_attempt_age_s);
    cJSON_AddNumberToObject(object, "refresh_count",
                            weather.refresh_count);
    cJSON_AddNumberToObject(object, "refresh_interval_s", 30 * 60);
    cJSON_AddStringToObject(object, "last_error", weather.last_error);

    cJSON_AddBoolToObject(current, "valid", weather.current_valid);
    cJSON_AddNumberToObject(current, "temperature_c",
                            weather.temperature_c);
    cJSON_AddNumberToObject(current, "apparent_temperature_c",
                            weather.apparent_temperature_c);
    cJSON_AddNumberToObject(current, "humidity_percent",
                            weather.humidity_percent);
    cJSON_AddNumberToObject(current, "weather_code",
                            weather.weather_code);
    cJSON_AddBoolToObject(current, "is_day", weather.is_day);

    cJSON_AddBoolToObject(daily, "valid", weather.daily_valid);
    cJSON_AddNumberToObject(daily, "weather_code",
                            weather.daily_weather_code);
    cJSON_AddNumberToObject(daily, "temperature_max_c",
                            weather.temperature_max_c);
    cJSON_AddNumberToObject(daily, "temperature_min_c",
                            weather.temperature_min_c);
    return true;
}

static const char *care_alert_mode_name(care_alert_mode_t mode)
{
    switch (mode) {
    case CARE_ALERT_MODE_VIBRATE:
        return "vibrate";
    case CARE_ALERT_MODE_SOUND:
        return "sound";
    case CARE_ALERT_MODE_SILENT:
        return "silent";
    case CARE_ALERT_MODE_BOTH:
    default:
        return "both";
    }
}

static bool parse_care_alert_mode(const char *name, care_alert_mode_t *mode)
{
    if (name == NULL || mode == NULL) {
        return false;
    }
    if (strcmp(name, "both") == 0) {
        *mode = CARE_ALERT_MODE_BOTH;
    } else if (strcmp(name, "vibrate") == 0) {
        *mode = CARE_ALERT_MODE_VIBRATE;
    } else if (strcmp(name, "sound") == 0) {
        *mode = CARE_ALERT_MODE_SOUND;
    } else if (strcmp(name, "silent") == 0) {
        *mode = CARE_ALERT_MODE_SILENT;
    } else {
        return false;
    }
    return true;
}

static bool add_care_object(cJSON *wellness)
{
    care_snapshot_t care;
    cJSON *object;
    cJSON *settings;

    care_guidance_get_snapshot(&care);
    object = cJSON_AddObjectToObject(wellness, "care");
    if (object == NULL) {
        return false;
    }

    cJSON_AddBoolToObject(object, "time_valid", care.time_valid);
    cJSON_AddBoolToObject(object, "within_work", care.within_work);
    cJSON_AddBoolToObject(object, "overtime_active", care.overtime_active);
    cJSON_AddBoolToObject(object, "water_due", care.water_due);
    cJSON_AddNumberToObject(object, "today_sitting_seconds",
                            care.today_sitting_seconds);
    cJSON_AddNumberToObject(object, "longest_sitting_seconds",
                            care.longest_sitting_seconds);
    cJSON_AddNumberToObject(object, "current_sitting_seconds",
                            care.current_sitting_seconds);
    cJSON_AddNumberToObject(object, "completed_breaks",
                            care.completed_breaks);
    cJSON_AddNumberToObject(object, "gentle_nudges",
                            care.gentle_nudges);
    cJSON_AddNumberToObject(object, "activity_nudges",
                            care.activity_nudges);
    cJSON_AddNumberToObject(object, "water_ml", care.water_ml);
    cJSON_AddNumberToObject(object, "water_goal_ml",
                            care.water_goal_ml);
    cJSON_AddNumberToObject(object, "water_remaining_ml",
                            care.water_remaining_ml);
    cJSON_AddNumberToObject(object, "next_water_seconds",
                            care.next_water_seconds);
    cJSON_AddNumberToObject(object, "scheduled_water_cups",
                            care.scheduled_water_cups);
    cJSON_AddNumberToObject(object, "completed_water_cups",
                            care.completed_water_cups);
    cJSON_AddNumberToObject(object, "care_level", care.care_level);
    cJSON_AddStringToObject(object, "care_action", care.care_action);

    settings = cJSON_AddObjectToObject(object, "settings");
    if (settings == NULL) {
        return false;
    }
    cJSON_AddBoolToObject(settings, "water_enabled",
                          care.settings.water_enabled);
    cJSON_AddNumberToObject(settings, "daily_goal_ml",
                            care.settings.daily_goal_ml);
    cJSON_AddNumberToObject(settings, "cup_ml", care.settings.cup_ml);
    cJSON_AddNumberToObject(settings, "work_start_minutes",
                            care.settings.work_start_min);
    cJSON_AddNumberToObject(settings, "morning_end_minutes",
                            care.settings.morning_end_min);
    cJSON_AddNumberToObject(settings, "afternoon_start_minutes",
                            care.settings.afternoon_start_min);
    cJSON_AddNumberToObject(settings, "work_end_minutes",
                            care.settings.work_end_min);
    cJSON_AddBoolToObject(settings, "overtime_enabled",
                          care.settings.overtime_enabled);
    cJSON_AddNumberToObject(settings, "overtime_end_minutes",
                            care.settings.overtime_end_min);
    cJSON_AddStringToObject(settings, "alert_mode",
                            care_alert_mode_name(care.settings.alert_mode));
    return true;
}

static bool add_led_object(cJSON *parent, const char *name, bool configured)
{
    rgb_indicator_snapshot_t led = {
        .mode = RGB_INDICATOR_MODE_AUTO,
        .brightness = 45u,
    };
    cJSON *object;
    bool initialized = rgb_indicator_get_snapshot(&led);

    if (parent == NULL || name == NULL) {
        return false;
    }

    object = cJSON_AddObjectToObject(parent, name);
    if (object == NULL) {
        return false;
    }

    cJSON_AddBoolToObject(object, "enabled", configured);
    cJSON_AddBoolToObject(object, "ready", initialized && led.ready);
    cJSON_AddStringToObject(object, "mode",
                            rgb_indicator_mode_name(led.mode));
    cJSON_AddNumberToObject(object, "r", led.red);
    cJSON_AddNumberToObject(object, "g", led.green);
    cJSON_AddNumberToObject(object, "b", led.blue);
    cJSON_AddNumberToObject(object, "brightness", led.brightness);
    cJSON_AddBoolToObject(object, "wellness_active",
                          led.wellness_active);
    cJSON_AddBoolToObject(object, "wellness_present",
                          led.wellness_present);
    cJSON_AddBoolToObject(object, "alert_active", led.wellness_alert);
    cJSON_AddBoolToObject(object, "water_due", led.water_due);
    return true;
}

static esp_err_t send_json_response(httpd_req_t *request, cJSON *root)
{
    char *payload;
    esp_err_t err;

    if (root == NULL) {
        return httpd_resp_send_500(request);
    }

    payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (payload == NULL) {
        return httpd_resp_send_500(request);
    }

    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    err = httpd_resp_send(request, payload, HTTPD_RESP_USE_STRLEN);
    cJSON_free(payload);
    return err;
}

static esp_err_t send_json_error(httpd_req_t *request, const char *status,
                                 const char *code, const char *message)
{
    cJSON *root = cJSON_CreateObject();

    if (root == NULL) {
        return httpd_resp_send_500(request);
    }
    cJSON_AddBoolToObject(root, "ok", false);
    cJSON_AddStringToObject(root, "error", code);
    cJSON_AddStringToObject(root, "message", message);
    httpd_resp_set_status(request, status);
    return send_json_response(request, root);
}

static esp_err_t receive_json_body(httpd_req_t *request, cJSON **json)
{
    char *body;
    size_t received = 0;
    int read_size;

    if (json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *json = NULL;

    if (request->content_len <= 0 ||
        request->content_len > HTTP_API_JSON_BODY_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    body = malloc((size_t)request->content_len + 1);
    if (body == NULL) {
        return ESP_ERR_NO_MEM;
    }

    while (received < (size_t)request->content_len) {
        read_size = httpd_req_recv(
            request, body + received,
            (size_t)request->content_len - received);
        if (read_size <= 0) {
            free(body);
            return read_size == HTTPD_SOCK_ERR_TIMEOUT
                       ? ESP_ERR_TIMEOUT
                       : ESP_FAIL;
        }
        received += (size_t)read_size;
    }
    body[received] = '\0';

    *json = cJSON_Parse(body);
    free(body);
    return *json == NULL ? ESP_ERR_INVALID_ARG : ESP_OK;
}

static void set_http_status_for_error(httpd_req_t *request, esp_err_t err,
                                      const char *code, const char *message)
{
    const char *status = "500 Internal Server Error";

    if (err == ESP_ERR_INVALID_ARG || err == ESP_ERR_INVALID_SIZE) {
        status = "400 Bad Request";
    } else if (err == ESP_ERR_INVALID_STATE) {
        status = "409 Conflict";
    } else if (err == ESP_ERR_TIMEOUT) {
        status = "408 Request Timeout";
    } else if (err == ESP_ERR_NO_MEM) {
        status = "503 Service Unavailable";
    }

    send_json_error(request, status, code, message);
}

static esp_err_t status_get_handler(httpd_req_t *request)
{
    const app_config_t *config = app_config_get();
    wifi_manager_status_t wifi_status;
    config_service_status_t storage_status = {0};
    wellness_snapshot_t wellness_snapshot = {0};
    cJSON *root = cJSON_CreateObject();
    cJSON *wifi = NULL;
    cJSON *storage = NULL;
    cJSON *network = NULL;
    cJSON *wellness = NULL;
    cJSON *sensors = NULL;
    cJSON *pir = NULL;
    cJSON *sht30 = NULL;
    cJSON *oled = NULL;
    cJSON *sedentary = NULL;
    cJSON *wellness_settings = NULL;
    bool wellness_running = wellness_monitor_get_snapshot(&wellness_snapshot);

    wifi_manager_get_status(&wifi_status);
    config_service_get_status(&storage_status);

    ESP_LOGI(TAG, "GET %s", request->uri);

    if (root == NULL) {
        return httpd_resp_send_500(request);
    }

    wifi = cJSON_AddObjectToObject(root, "wifi");
    storage = cJSON_AddObjectToObject(root, "storage");
    network = cJSON_AddObjectToObject(root, "network");
    wellness = cJSON_AddObjectToObject(root, "wellness");
    if (wifi == NULL || storage == NULL || network == NULL ||
        wellness == NULL) {
        cJSON_Delete(root);
        return httpd_resp_send_500(request);
    }

    sensors = cJSON_AddObjectToObject(wellness, "sensors");
    sedentary = cJSON_AddObjectToObject(wellness, "sedentary");
    wellness_settings = cJSON_AddObjectToObject(wellness, "settings");
    if (sensors == NULL || sedentary == NULL || wellness_settings == NULL) {
        cJSON_Delete(root);
        return httpd_resp_send_500(request);
    }

    pir = cJSON_AddObjectToObject(sensors, "pir");
    sht30 = cJSON_AddObjectToObject(sensors, "sht30");
    oled = cJSON_AddObjectToObject(sensors, "oled");
    if (pir == NULL || sht30 == NULL || oled == NULL) {
        cJSON_Delete(root);
        return httpd_resp_send_500(request);
    }

    cJSON_AddStringToObject(root, "device", config->device_name);
    cJSON_AddStringToObject(root, "version", config->version);
    cJSON_AddNumberToObject(root, "uptime_s",
                            (double)(esp_timer_get_time() / 1000000LL));
    cJSON_AddStringToObject(wifi, "mode", wifi_manager_mode_name());
    cJSON_AddBoolToObject(wifi, "station_connected",
                          wifi_manager_sta_connected());
    cJSON_AddBoolToObject(wifi, "station_configured",
                          wifi_status.configured);
    cJSON_AddBoolToObject(wifi, "station_saved", wifi_status.saved);
    cJSON_AddBoolToObject(wifi, "station_connecting",
                          wifi_status.connecting);
    cJSON_AddStringToObject(wifi, "station_ssid", wifi_status.ssid);
    cJSON_AddStringToObject(wifi, "station_ip", wifi_status.ip);
    cJSON_AddNumberToObject(wifi, "station_rssi", wifi_status.rssi);
    cJSON_AddStringToObject(wifi, "station_last_error",
                            wifi_status.last_error);
    cJSON_AddBoolToObject(storage, "initialized", storage_status.initialized);
    cJSON_AddBoolToObject(storage, "recovery_required",
                          storage_status.recovery_required);
    cJSON_AddNumberToObject(storage, "last_error",
                            storage_status.last_error);
    cJSON_AddStringToObject(storage, "last_error_name",
                            esp_err_to_name(storage_status.last_error));
    cJSON_AddBoolToObject(storage, "flash_encryption_enabled",
                          esp_flash_encryption_enabled());
    cJSON_AddNumberToObject(wifi, "ap_clients",
                            wifi_manager_ap_client_count());
    cJSON_AddStringToObject(network, "softap_ssid", config->wifi.ap_ssid);
    cJSON_AddStringToObject(network, "softap_ip", "192.168.4.1");
    if (!add_time_object(root)) {
        cJSON_Delete(root);
        return httpd_resp_send_500(request);
    }
    if (!add_weather_object(root)) {
        cJSON_Delete(root);
        return httpd_resp_send_500(request);
    }
    if (!add_web_settings_object(root)) {
        cJSON_Delete(root);
        return httpd_resp_send_500(request);
    }
    if (!add_startup_object(root)) {
        cJSON_Delete(root);
        return httpd_resp_send_500(request);
    }

    cJSON_AddBoolToObject(wellness, "enabled", config->wellness.enabled);
    cJSON_AddBoolToObject(wellness, "running", wellness_running);
    cJSON_AddBoolToObject(pir, "present", wellness_snapshot.pir_present);
    cJSON_AddBoolToObject(pir, "motion", wellness_snapshot.pir_motion);
    cJSON_AddNumberToObject(pir, "motion_events",
                            wellness_snapshot.pir_motion_events);
    cJSON_AddBoolToObject(sht30, "valid", wellness_snapshot.sht30_valid);
    cJSON_AddBoolToObject(sht30, "online", wellness_snapshot.sht30_online);
    cJSON_AddNumberToObject(
        sht30, "temperature_c",
        (double)wellness_snapshot.temperature_tenths_c / 10.0);
    cJSON_AddNumberToObject(
        sht30, "humidity_percent",
        (double)wellness_snapshot.humidity_tenths_rh / 10.0);
    cJSON_AddNumberToObject(sht30, "age_ms", wellness_snapshot.sht30_age_ms);
    cJSON_AddNumberToObject(sht30, "frames",
                            wellness_snapshot.sht30_frames);
    cJSON_AddNumberToObject(sht30, "bad_frames",
                            wellness_snapshot.sht30_bad_frames);
    cJSON_AddBoolToObject(oled, "ready", wellness_snapshot.oled_ready);
    cJSON_AddNumberToObject(oled, "address", wellness_snapshot.oled_address);

    cJSON_AddBoolToObject(sedentary, "active",
                          wellness_snapshot.sedentary_active);
    cJSON_AddBoolToObject(sedentary, "present",
                          wellness_snapshot.sedentary_present);
    cJSON_AddBoolToObject(sedentary, "alert_active",
                          wellness_snapshot.sedentary_alert_active);
    cJSON_AddStringToObject(
        sedentary, "phase",
        wellness_snapshot.sedentary_phase != NULL
            ? wellness_snapshot.sedentary_phase
            : "unknown");
    cJSON_AddBoolToObject(sedentary, "beeping",
                          wellness_snapshot.beeping);
    cJSON_AddBoolToObject(sedentary, "vibrating",
                          wellness_snapshot.vibrating);
    cJSON_AddNumberToObject(sedentary, "beep_count",
                            wellness_snapshot.beep_count);
    cJSON_AddNumberToObject(
        sedentary, "sitting_seconds",
        wellness_snapshot.sitting_elapsed_ms / 1000u);
    cJSON_AddNumberToObject(
        sedentary, "next_reminder_seconds",
        wellness_snapshot.next_reminder_ms / 1000u);
    cJSON_AddNumberToObject(
        sedentary, "break_remaining_seconds",
        wellness_snapshot.break_remaining_ms / 1000u);
    cJSON_AddNumberToObject(sedentary, "alert_starts",
                            wellness_snapshot.alert_starts);
    cJSON_AddNumberToObject(sedentary, "escalations",
                            wellness_snapshot.escalations);
    cJSON_AddNumberToObject(sedentary, "gentle_nudges",
                            wellness_snapshot.gentle_nudges);
    cJSON_AddNumberToObject(sedentary, "activity_nudges",
                            wellness_snapshot.activity_nudges);
    cJSON_AddNumberToObject(sedentary, "short_breaks",
                            wellness_snapshot.short_breaks);
    cJSON_AddNumberToObject(sedentary, "completed_breaks",
                            wellness_snapshot.completed_breaks);
    cJSON_AddNumberToObject(sedentary, "reminder_level",
                            wellness_snapshot.reminder_level);

    cJSON_AddNumberToObject(
        wellness_settings, "initial_minutes",
        config->wellness.sedentary_initial_ms / 60000u);
    cJSON_AddNumberToObject(
        wellness_settings, "escalation_minutes",
        config->wellness.sedentary_escalation_ms / 60000u);
    cJSON_AddNumberToObject(
        wellness_settings, "break_minutes",
        config->wellness.sedentary_break_ms / 60000u);
    cJSON_AddNumberToObject(
        wellness_settings, "absence_seconds",
        config->wellness.sedentary_absence_ms / 1000u);
    cJSON_AddBoolToObject(wellness_settings, "buzzer_enabled",
                          config->wellness.buzzer_enabled);
    cJSON_AddBoolToObject(wellness_settings, "vibration_enabled",
                          config->wellness.vibration_enabled);
    cJSON_AddNumberToObject(wellness_settings, "vibration_gpio",
                            config->wellness.vibration_gpio);
    cJSON_AddBoolToObject(wellness_settings, "rgb_led_enabled",
                          config->wellness.rgb_led_enabled);
    cJSON_AddNumberToObject(wellness_settings, "rgb_red_gpio",
                            config->wellness.rgb_red_gpio);
    cJSON_AddNumberToObject(wellness_settings, "rgb_green_gpio",
                            config->wellness.rgb_green_gpio);
    cJSON_AddNumberToObject(wellness_settings, "rgb_blue_gpio",
                            config->wellness.rgb_blue_gpio);
    if (!add_led_object(wellness, "led",
                        config->wellness.rgb_led_enabled)) {
        cJSON_Delete(root);
        return httpd_resp_send_500(request);
    }
    if (!add_care_object(wellness)) {
        cJSON_Delete(root);
        return httpd_resp_send_500(request);
    }

    return send_json_response(request, root);
}

static esp_err_t wifi_scan_get_handler(httpd_req_t *request)
{
    wifi_manager_scan_result_t results[WIFI_MANAGER_SCAN_MAX_RESULTS];
    size_t count = 0;
    esp_err_t err;
    cJSON *root;
    cJSON *items;

    ESP_LOGI(TAG, "GET %s", request->uri);

    err = wifi_manager_scan(results, WIFI_MANAGER_SCAN_MAX_RESULTS, &count);
    if (err != ESP_OK) {
        set_http_status_for_error(
            request, err, "wifi_scan_failed",
            err == ESP_ERR_INVALID_STATE
                ? "Wi-Fi 尚未就绪"
                : "扫描 Wi-Fi 失败，请稍后重试");
        return ESP_OK;
    }

    root = cJSON_CreateObject();
    if (root == NULL) {
        return httpd_resp_send_500(request);
    }

    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddNumberToObject(root, "count", (double)count);
    items = cJSON_AddArrayToObject(root, "results");
    if (items == NULL) {
        cJSON_Delete(root);
        return httpd_resp_send_500(request);
    }

    for (size_t index = 0; index < count; index++) {
        cJSON *item = cJSON_CreateObject();

        if (item == NULL) {
            cJSON_Delete(root);
            return httpd_resp_send_500(request);
        }
        cJSON_AddStringToObject(item, "ssid", results[index].ssid);
        cJSON_AddNumberToObject(item, "rssi", results[index].rssi);
        cJSON_AddBoolToObject(item, "secure", results[index].secure);
        cJSON_AddItemToArray(items, item);
    }

    return send_json_response(request, root);
}

static esp_err_t wifi_post_handler(httpd_req_t *request)
{
    cJSON *root = NULL;
    cJSON *ssid_item;
    cJSON *password_item;
    const char *ssid;
    const char *password = "";
    esp_err_t err;
    cJSON *response;

    ESP_LOGI(TAG, "POST %s", request->uri);

    err = receive_json_body(request, &root);
    if (err != ESP_OK) {
        set_http_status_for_error(
            request, err, "invalid_json",
            err == ESP_ERR_INVALID_SIZE
                ? "请求内容为空或过大"
                : "请求不是有效的 JSON");
        return ESP_OK;
    }

    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        send_json_error(request, "400 Bad Request", "invalid_json",
                        "请求内容必须是 JSON 对象");
        return ESP_OK;
    }

    ssid_item = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    password_item = cJSON_GetObjectItemCaseSensitive(root, "password");
    if (!cJSON_IsString(ssid_item) || ssid_item->valuestring == NULL ||
        ssid_item->valuestring[0] == '\0' ||
        strlen(ssid_item->valuestring) > WIFI_MANAGER_SSID_MAX_LEN) {
        cJSON_Delete(root);
        send_json_error(request, "400 Bad Request", "invalid_ssid",
                        "Wi-Fi 名称不能为空且不能超过 32 个字符");
        return ESP_OK;
    }
    if (password_item != NULL &&
        (!cJSON_IsString(password_item) ||
         password_item->valuestring == NULL)) {
        cJSON_Delete(root);
        send_json_error(request, "400 Bad Request", "invalid_password",
                        "Wi-Fi 密码必须是字符串");
        return ESP_OK;
    }

    ssid = ssid_item->valuestring;
    if (password_item != NULL) {
        password = password_item->valuestring;
    }

    err = wifi_manager_connect_station(ssid, password);
    cJSON_Delete(root);

    if (err != ESP_OK) {
        set_http_status_for_error(
            request, err, "wifi_connect_failed",
            err == ESP_ERR_INVALID_ARG
                ? "Wi-Fi 密码需留空或为 8 至 64 个字符"
                : err == ESP_ERR_INVALID_STATE
                    ? "Wi-Fi 服务尚未就绪"
                    : "无法发起 Wi-Fi 连接");
        return ESP_OK;
    }

    response = cJSON_CreateObject();
    if (response == NULL) {
        return httpd_resp_send_500(request);
    }
    cJSON_AddBoolToObject(response, "ok", true);
    cJSON_AddBoolToObject(response, "connecting", true);
    cJSON_AddStringToObject(response, "ssid", ssid);
    httpd_resp_set_status(request, "202 Accepted");
    return send_json_response(request, response);
}

static esp_err_t time_get_handler(httpd_req_t *request)
{
    cJSON *root = cJSON_CreateObject();

    ESP_LOGI(TAG, "GET %s", request->uri);
    if (root == NULL) {
        return httpd_resp_send_500(request);
    }

    if (!fill_time_object(root)) {
        cJSON_Delete(root);
        return httpd_resp_send_500(request);
    }

    return send_json_response(request, root);
}

static bool json_time_setup_value(cJSON *root, const char *name,
                                  uint16_t *value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);

    if (!cJSON_IsNumber(item) || item->valuedouble < 0 ||
        item->valuedouble >= 24 * 60 ||
        item->valuedouble != (double)(int)item->valuedouble) {
        return false;
    }
    *value = (uint16_t)item->valueint;
    return true;
}

static esp_err_t time_post_handler(httpd_req_t *request)
{
    cJSON *root = NULL;
    cJSON *unix_time_item;
    cJSON *response;
    esp_err_t err;

    ESP_LOGI(TAG, "POST %s", request->uri);
    err = receive_json_body(request, &root);
    if (err != ESP_OK) {
        set_http_status_for_error(request, err, "invalid_json",
                                  "请求不是有效的 JSON");
        return ESP_OK;
    }

    unix_time_item =
        cJSON_GetObjectItemCaseSensitive(root, "unix_time");
    if (!cJSON_IsNumber(unix_time_item)) {
        cJSON_Delete(root);
        send_json_error(request, "400 Bad Request", "invalid_unix_time",
                        "unix_time 必须是 Unix 秒数");
        return ESP_OK;
    }

    err = time_service_set_unix_time(
        (int64_t)unix_time_item->valuedouble);
    cJSON_Delete(root);
    if (err != ESP_OK) {
        set_http_status_for_error(request, err, "time_sync_failed",
                                  "无法设置设备时间");
        return ESP_OK;
    }

    response = cJSON_CreateObject();
    if (response == NULL) {
        return httpd_resp_send_500(request);
    }
    cJSON_AddBoolToObject(response, "ok", true);
    if (!add_time_object(response)) {
        cJSON_Delete(response);
        return httpd_resp_send_500(request);
    }
    return send_json_response(request, response);
}

static esp_err_t weather_settings_post_handler(httpd_req_t *request)
{
    cJSON *root = NULL;
    cJSON *enabled;
    cJSON *location;
    cJSON *latitude;
    cJSON *longitude;
    weather_service_settings_t settings = {0};
    cJSON *response;
    esp_err_t err;

    ESP_LOGI(TAG, "POST %s", request->uri);
    err = receive_json_body(request, &root);
    if (err != ESP_OK) {
        set_http_status_for_error(request, err, "invalid_json",
                                  "请求不是有效的 JSON");
        return ESP_OK;
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        send_json_error(request, "400 Bad Request", "invalid_json",
                        "请求内容必须是 JSON 对象");
        return ESP_OK;
    }

    enabled = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    location = cJSON_GetObjectItemCaseSensitive(root, "location");
    latitude = cJSON_GetObjectItemCaseSensitive(root, "latitude");
    longitude = cJSON_GetObjectItemCaseSensitive(root, "longitude");
    if (!cJSON_IsBool(enabled) ||
        !cJSON_IsString(location) || location->valuestring == NULL ||
        location->valuestring[0] == '\0' ||
        strlen(location->valuestring) >
            WEATHER_SERVICE_LOCATION_MAX_LEN ||
        !cJSON_IsNumber(latitude) ||
        !cJSON_IsNumber(longitude) ||
        !isfinite(latitude->valuedouble) ||
        !isfinite(longitude->valuedouble) ||
        latitude->valuedouble < -90.0 ||
        latitude->valuedouble > 90.0 ||
        longitude->valuedouble < -180.0 ||
        longitude->valuedouble > 180.0) {
        cJSON_Delete(root);
        send_json_error(
            request, "400 Bad Request", "invalid_weather_settings",
            "请填写位置名称，并输入有效纬度 -90 至 90、经度 -180 至 180");
        return ESP_OK;
    }

    settings.enabled = cJSON_IsTrue(enabled);
    snprintf(settings.location, sizeof(settings.location), "%s",
             location->valuestring);
    settings.latitude = latitude->valuedouble;
    settings.longitude = longitude->valuedouble;
    err = weather_service_set_settings(&settings);
    cJSON_Delete(root);
    if (err != ESP_OK) {
        set_http_status_for_error(request, err, "weather_settings_failed",
                                  "无法保存天气位置设置");
        return ESP_OK;
    }

    response = cJSON_CreateObject();
    if (response == NULL) {
        return httpd_resp_send_500(request);
    }
    cJSON_AddBoolToObject(response, "ok", true);
    if (!add_weather_object(response)) {
        cJSON_Delete(response);
        return httpd_resp_send_500(request);
    }
    return send_json_response(request, response);
}

static esp_err_t weather_refresh_post_handler(httpd_req_t *request)
{
    cJSON *response;
    esp_err_t err;

    ESP_LOGI(TAG, "POST %s", request->uri);
    err = weather_service_request_refresh();
    if (err != ESP_OK) {
        set_http_status_for_error(
            request, err, "weather_refresh_unavailable",
            err == ESP_ERR_INVALID_STATE
                ? "天气服务尚未启动或已在设置中关闭"
                : "无法请求天气刷新");
        return ESP_OK;
    }

    response = cJSON_CreateObject();
    if (response == NULL) {
        return httpd_resp_send_500(request);
    }
    cJSON_AddBoolToObject(response, "ok", true);
    cJSON_AddBoolToObject(response, "requested", true);
    httpd_resp_set_status(request, "202 Accepted");
    return send_json_response(request, response);
}

static esp_err_t care_settings_post_handler(httpd_req_t *request)
{
    cJSON *root = NULL;
    cJSON *water_enabled;
    cJSON *overtime_enabled;
    cJSON *alert_mode;
    cJSON *goal;
    cJSON *cup;
    care_snapshot_t current = {0};
    care_settings_t settings;
    care_alert_mode_t requested_alert_mode = CARE_ALERT_MODE_BOTH;
    cJSON *response;
    esp_err_t err;
    bool fields_valid;

    ESP_LOGI(TAG, "POST %s", request->uri);
    err = receive_json_body(request, &root);
    if (err != ESP_OK) {
        set_http_status_for_error(request, err, "invalid_json",
                                  "请求不是有效的 JSON");
        return ESP_OK;
    }

    care_guidance_get_snapshot(&current);
    settings = current.settings;
    water_enabled =
        cJSON_GetObjectItemCaseSensitive(root, "water_enabled");
    overtime_enabled =
        cJSON_GetObjectItemCaseSensitive(root, "overtime_enabled");
    alert_mode = cJSON_GetObjectItemCaseSensitive(root, "alert_mode");
    goal = cJSON_GetObjectItemCaseSensitive(root, "daily_goal_ml");
    cup = cJSON_GetObjectItemCaseSensitive(root, "cup_ml");
    if (alert_mode != NULL &&
        (!cJSON_IsString(alert_mode) ||
         !parse_care_alert_mode(alert_mode->valuestring,
                                &requested_alert_mode))) {
        cJSON_Delete(root);
        send_json_error(request, "400 Bad Request", "invalid_alert_mode",
                        "alert_mode 必须是 both、vibrate、sound 或 silent");
        return ESP_OK;
    }
    fields_valid =
        cJSON_IsBool(water_enabled) &&
        cJSON_IsBool(overtime_enabled) &&
        cJSON_IsNumber(goal) && cJSON_IsNumber(cup) &&
        json_time_setup_value(root, "work_start_minutes",
                              &settings.work_start_min) &&
        json_time_setup_value(root, "morning_end_minutes",
                              &settings.morning_end_min) &&
        json_time_setup_value(root, "afternoon_start_minutes",
                              &settings.afternoon_start_min) &&
        json_time_setup_value(root, "work_end_minutes",
                              &settings.work_end_min) &&
        json_time_setup_value(root, "overtime_end_minutes",
                              &settings.overtime_end_min);
    if (!fields_valid) {
        cJSON_Delete(root);
        send_json_error(request, "400 Bad Request", "invalid_care_settings",
                        "关怀设置字段不完整或格式错误");
        return ESP_OK;
    }

    settings.water_enabled = cJSON_IsTrue(water_enabled);
    settings.overtime_enabled = cJSON_IsTrue(overtime_enabled);
    settings.daily_goal_ml = (uint16_t)goal->valueint;
    settings.cup_ml = (uint16_t)cup->valueint;
    if (alert_mode != NULL) {
        settings.alert_mode = requested_alert_mode;
    }
    err = care_guidance_set_settings(&settings);
    cJSON_Delete(root);
    if (err != ESP_OK) {
        if (err == ESP_ERR_INVALID_ARG) {
            set_http_status_for_error(
                request, err, "invalid_care_settings",
                "作息时段或饮水目标无效：目标 0.5-5 L，单杯 100-500 ml，"
                "上下班时间需按先后排列");
        } else {
            set_http_status_for_error(
                request, err, "care_settings_save_failed",
                "关怀设置暂时无法保存，请检查本地存储状态");
        }
        return ESP_OK;
    }

    response = cJSON_CreateObject();
    if (response == NULL) {
        return httpd_resp_send_500(request);
    }
    cJSON_AddBoolToObject(response, "ok", true);
    if (!add_care_object(response)) {
        cJSON_Delete(response);
        return httpd_resp_send_500(request);
    }
    return send_json_response(request, response);
}

static bool read_web_page_mask(cJSON *root, uint8_t *page_mask)
{
    cJSON *pages;
    uint8_t mask = 0u;

    if (root == NULL || page_mask == NULL || !cJSON_IsObject(root)) {
        return false;
    }
    pages = cJSON_GetObjectItemCaseSensitive(root, "pages");
    if (!cJSON_IsObject(pages)) {
        return false;
    }

    for (size_t page = 0u; page < (size_t)WEB_PAGE_COUNT; page++) {
        cJSON *enabled = cJSON_GetObjectItemCaseSensitive(
            pages, s_web_page_names[page]);

        if (!cJSON_IsBool(enabled)) {
            return false;
        }
        if (cJSON_IsTrue(enabled)) {
            mask |= (uint8_t)(1u << page);
        }
    }
    if (!is_valid_web_page_mask(mask)) {
        return false;
    }

    *page_mask = mask;
    return true;
}

static esp_err_t web_settings_post_handler(httpd_req_t *request)
{
    cJSON *root = NULL;
    cJSON *response;
    uint8_t page_mask;
    esp_err_t err;

    ESP_LOGI(TAG, "POST %s", request->uri);
    err = receive_json_body(request, &root);
    if (err != ESP_OK) {
        set_http_status_for_error(request, err, "invalid_json",
                                  "请求不是有效的 JSON");
        return ESP_OK;
    }
    if (!read_web_page_mask(root, &page_mask)) {
        cJSON_Delete(root);
        send_json_error(
            request, "400 Bad Request", "invalid_web_page_settings",
            "pages 必须包含全部页面开关，并至少开启一个页面");
        return ESP_OK;
    }

    err = save_web_page_settings(page_mask);
    cJSON_Delete(root);
    if (err != ESP_OK) {
        set_http_status_for_error(request, err, "web_settings_save_failed",
                                  "无法保存页面显示设置");
        return ESP_OK;
    }

    response = cJSON_CreateObject();
    if (response == NULL) {
        return httpd_resp_send_500(request);
    }
    cJSON_AddBoolToObject(response, "ok", true);
    if (!add_web_settings_object(response)) {
        cJSON_Delete(response);
        return httpd_resp_send_500(request);
    }
    return send_json_response(request, response);
}

static esp_err_t water_post_handler(httpd_req_t *request)
{
    cJSON *root = NULL;
    cJSON *delta;
    cJSON *response;
    esp_err_t err;

    ESP_LOGI(TAG, "POST %s", request->uri);
    err = receive_json_body(request, &root);
    if (err != ESP_OK) {
        set_http_status_for_error(request, err, "invalid_json",
                                  "请求不是有效的 JSON");
        return ESP_OK;
    }

    delta = cJSON_GetObjectItemCaseSensitive(root, "delta_ml");
    if (!cJSON_IsNumber(delta) ||
        delta->valuedouble != (double)(int)delta->valuedouble) {
        cJSON_Delete(root);
        send_json_error(request, "400 Bad Request", "invalid_delta_ml",
                        "delta_ml 必须是非零整数");
        return ESP_OK;
    }

    err = care_guidance_add_water((int32_t)delta->valueint);
    cJSON_Delete(root);
    if (err != ESP_OK) {
        if (err == ESP_ERR_INVALID_ARG) {
            set_http_status_for_error(
                request, err, "invalid_delta_ml",
                "单次增减需在 1000 ml 以内");
        } else {
            set_http_status_for_error(
                request, err, "water_update_failed",
                "饮水记录暂时无法保存，请检查本地存储状态");
        }
        return ESP_OK;
    }

    response = cJSON_CreateObject();
    if (response == NULL) {
        return httpd_resp_send_500(request);
    }
    cJSON_AddBoolToObject(response, "ok", true);
    if (!add_care_object(response)) {
        cJSON_Delete(response);
        return httpd_resp_send_500(request);
    }
    return send_json_response(request, response);
}

static bool read_json_byte(cJSON *root, const char *name, bool required,
                           uint8_t fallback, uint8_t maximum,
                           uint8_t *value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);

    if (item == NULL) {
        if (required) {
            return false;
        }
        *value = fallback;
        return true;
    }
    if (!cJSON_IsNumber(item) || item->valuedouble < 0 ||
        item->valuedouble > maximum ||
        item->valuedouble != (double)(int)item->valuedouble) {
        return false;
    }

    *value = (uint8_t)item->valueint;
    return true;
}

static esp_err_t led_post_handler(httpd_req_t *request)
{
    cJSON *root = NULL;
    cJSON *mode_item;
    rgb_indicator_snapshot_t current;
    rgb_indicator_mode_t mode;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t brightness;
    const char *mode_name;
    cJSON *response;
    esp_err_t err;

    ESP_LOGI(TAG, "POST %s", request->uri);
    if (!rgb_indicator_get_snapshot(&current)) {
        send_json_error(request, "409 Conflict", "led_unavailable",
                        "RGB 灯尚未初始化或当前固件未启用");
        return ESP_OK;
    }

    err = receive_json_body(request, &root);
    if (err != ESP_OK) {
        set_http_status_for_error(request, err, "invalid_json",
                                  "请求不是有效的 JSON");
        return ESP_OK;
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        send_json_error(request, "400 Bad Request", "invalid_json",
                        "请求内容必须是 JSON 对象");
        return ESP_OK;
    }

    mode_item = cJSON_GetObjectItemCaseSensitive(root, "mode");
    if (!cJSON_IsString(mode_item) || mode_item->valuestring == NULL) {
        cJSON_Delete(root);
        send_json_error(request, "400 Bad Request", "invalid_led_mode",
                        "mode 必须是 auto、solid、rainbow 或 off");
        return ESP_OK;
    }

    mode_name = mode_item->valuestring;
    if (strcmp(mode_name, "auto") == 0) {
        mode = RGB_INDICATOR_MODE_AUTO;
    } else if (strcmp(mode_name, "solid") == 0) {
        mode = RGB_INDICATOR_MODE_SOLID;
    } else if (strcmp(mode_name, "rainbow") == 0) {
        mode = RGB_INDICATOR_MODE_RAINBOW;
    } else if (strcmp(mode_name, "off") == 0) {
        mode = RGB_INDICATOR_MODE_OFF;
    } else {
        cJSON_Delete(root);
        send_json_error(request, "400 Bad Request", "invalid_led_mode",
                        "mode 必须是 auto、solid、rainbow 或 off");
        return ESP_OK;
    }

    red = current.red;
    green = current.green;
    blue = current.blue;
    brightness = current.brightness;
    if ((mode == RGB_INDICATOR_MODE_SOLID &&
         (!read_json_byte(root, "r", true, red, 255u, &red) ||
          !read_json_byte(root, "g", true, green, 255u, &green) ||
          !read_json_byte(root, "b", true, blue, 255u, &blue))) ||
        !read_json_byte(root, "brightness", false, brightness, 100u,
                        &brightness)) {
        cJSON_Delete(root);
        send_json_error(request, "400 Bad Request", "invalid_led_settings",
                        "RGB 值需为 0-255，亮度需为 0-100 的整数");
        return ESP_OK;
    }

    err = rgb_indicator_set_mode(mode, red, green, blue, brightness);
    cJSON_Delete(root);
    if (err != ESP_OK) {
        set_http_status_for_error(request, err, "led_update_failed",
                                  "无法保存或更新 RGB 灯设置");
        return ESP_OK;
    }

    response = cJSON_CreateObject();
    if (response == NULL) {
        return httpd_resp_send_500(request);
    }
    cJSON_AddBoolToObject(response, "ok", true);
    if (!add_led_object(response, "led", true)) {
        cJSON_Delete(response);
        return httpd_resp_send_500(request);
    }
    return send_json_response(request, response);
}

static esp_err_t send_static_content(httpd_req_t *request, const uint8_t *data,
                                     size_t size)
{
    return httpd_resp_send(request, (const char *)data, (ssize_t)size);
}

static esp_err_t root_get_handler(httpd_req_t *request)
{
    const size_t page_size =
        (size_t)(index_html_gz_end - index_html_gz_start);

    ESP_LOGI(TAG, "GET %s (gzip, %u bytes)", request->uri,
             (unsigned)page_size);
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Web-Build", "2026.11.09-r31");
    httpd_resp_set_hdr(request, "Content-Security-Policy",
                       "default-src 'self'; style-src 'self' 'unsafe-inline'; "
                       "script-src 'self' 'unsafe-inline'; connect-src 'self'; "
                       "img-src 'self' data:; manifest-src 'self'; "
                       "base-uri 'none'; form-action 'none'");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(request, "Content-Encoding", "gzip");
    return send_static_content(request, index_html_gz_start, page_size);
}

static esp_err_t manifest_get_handler(httpd_req_t *request)
{
    const size_t size = (size_t)(manifest_webmanifest_end -
                                 manifest_webmanifest_start);

    ESP_LOGI(TAG, "GET %s", request->uri);
    httpd_resp_set_type(request, "application/manifest+json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    return httpd_resp_send(request, (const char *)manifest_webmanifest_start,
                           size);
}

static esp_err_t icon_get_handler(httpd_req_t *request)
{
    const size_t size = (size_t)(icon_svg_end - icon_svg_start);

    ESP_LOGI(TAG, "GET %s", request->uri);
    httpd_resp_set_type(request, "image/svg+xml");
    httpd_resp_set_hdr(request, "Cache-Control", "public, max-age=86400");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    return httpd_resp_send(request, (const char *)icon_svg_start, size);
}

static esp_err_t http_error_handler(httpd_req_t *request, httpd_err_code_t error)
{
    ESP_LOGW(TAG, "HTTP error %d for %s", error, request->uri);
    return httpd_resp_send_err(request, error, NULL);
}

static const httpd_uri_t s_root_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_get_handler,
    .user_ctx = NULL,
};

static const httpd_uri_t s_status_uri = {
    .uri = "/api/v1/status",
    .method = HTTP_GET,
    .handler = status_get_handler,
    .user_ctx = NULL,
};

static const httpd_uri_t s_time_uri = {
    .uri = "/api/v1/time",
    .method = HTTP_GET,
    .handler = time_get_handler,
    .user_ctx = NULL,
};

static const httpd_uri_t s_time_post_uri = {
    .uri = "/api/v1/time",
    .method = HTTP_POST,
    .handler = time_post_handler,
    .user_ctx = NULL,
};

static const httpd_uri_t s_weather_settings_uri = {
    .uri = "/api/v1/weather/settings",
    .method = HTTP_POST,
    .handler = weather_settings_post_handler,
    .user_ctx = NULL,
};

static const httpd_uri_t s_weather_refresh_uri = {
    .uri = "/api/v1/weather/refresh",
    .method = HTTP_POST,
    .handler = weather_refresh_post_handler,
    .user_ctx = NULL,
};

static const httpd_uri_t s_care_settings_uri = {
    .uri = "/api/v1/wellness/settings",
    .method = HTTP_POST,
    .handler = care_settings_post_handler,
    .user_ctx = NULL,
};

static const httpd_uri_t s_web_settings_uri = {
    .uri = "/api/v1/web/settings",
    .method = HTTP_POST,
    .handler = web_settings_post_handler,
    .user_ctx = NULL,
};

static const httpd_uri_t s_water_uri = {
    .uri = "/api/v1/water",
    .method = HTTP_POST,
    .handler = water_post_handler,
    .user_ctx = NULL,
};

static const httpd_uri_t s_led_uri = {
    .uri = "/api/v1/led",
    .method = HTTP_POST,
    .handler = led_post_handler,
    .user_ctx = NULL,
};

static const httpd_uri_t s_wifi_scan_uri = {
    .uri = "/api/v1/wifi/scan",
    .method = HTTP_GET,
    .handler = wifi_scan_get_handler,
    .user_ctx = NULL,
};

static const httpd_uri_t s_wifi_uri = {
    .uri = "/api/v1/wifi",
    .method = HTTP_POST,
    .handler = wifi_post_handler,
    .user_ctx = NULL,
};

static const httpd_uri_t s_manifest_uri = {
    .uri = "/manifest.webmanifest",
    .method = HTTP_GET,
    .handler = manifest_get_handler,
    .user_ctx = NULL,
};

static const httpd_uri_t s_icon_uri = {
    .uri = "/icon.svg",
    .method = HTTP_GET,
    .handler = icon_get_handler,
    .user_ctx = NULL,
};

esp_err_t http_api_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    esp_err_t err;

    if (s_server != NULL) {
        return ESP_OK;
    }

    err = load_web_page_settings();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "web page settings unavailable; using defaults: %s",
                 esp_err_to_name(err));
    }

    config.stack_size = 6144;
    config.max_open_sockets = 4;
    config.max_uri_handlers = 14;
    config.max_resp_headers = 12;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 30;

    err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server start failed: %s", esp_err_to_name(err));
        return err;
    }

    err = httpd_register_uri_handler(s_server, &s_root_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "root URI registration failed: %s", esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &s_status_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP URI registration failed: %s", esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &s_time_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "time URI registration failed: %s", esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &s_time_post_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "time POST URI registration failed: %s",
                 esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &s_weather_settings_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "weather settings URI registration failed: %s",
                 esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &s_weather_refresh_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "weather refresh URI registration failed: %s",
                 esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &s_care_settings_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "care settings URI registration failed: %s",
                 esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &s_web_settings_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "web settings URI registration failed: %s",
                 esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &s_water_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "water URI registration failed: %s",
                 esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &s_led_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LED URI registration failed: %s",
                 esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &s_wifi_scan_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi scan URI registration failed: %s",
                 esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &s_wifi_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi URI registration failed: %s",
                 esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &s_manifest_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "manifest URI registration failed: %s",
                 esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_uri_handler(s_server, &s_icon_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "icon URI registration failed: %s",
                 esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    err = httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND,
                                     http_error_handler);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP error handler registration failed: %s",
                 esp_err_to_name(err));
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    ESP_LOGI(TAG, "APIs ready: status, time, weather, wellness settings, "
                  "web settings, water, LED, wifi and wifi scan");
    return ESP_OK;
}
