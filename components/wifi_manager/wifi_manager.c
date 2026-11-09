#include "wifi_manager.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_events.h"
#include "config_service.h"
#include "esp_event.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define WIFI_MANAGER_RECONNECT_DELAY_US (3 * 1000 * 1000)

static const char *TAG = "wifi_manager";

static bool s_initialized;
static bool s_started;
static bool s_sta_connected;
static bool s_sta_connecting;
static bool s_sta_configured;
static bool s_sta_saved;
static bool s_manual_attempt;
static bool s_reconfiguring;
static uint32_t s_sta_config_generation = 1u;
static uint8_t s_ap_client_count;
static char s_ap_ssid[WIFI_MANAGER_SSID_MAX_LEN + 1];
static char s_ap_password[WIFI_MANAGER_PASSWORD_MAX_LEN + 1];
static uint8_t s_ap_channel;
static uint8_t s_ap_max_clients;
static char s_sta_ssid[WIFI_MANAGER_SSID_MAX_LEN + 1];
static char s_sta_password[WIFI_MANAGER_PASSWORD_MAX_LEN + 1];
static char s_sta_ip[16] = "0.0.0.0";
static char s_last_error[WIFI_MANAGER_STATUS_ERROR_MAX_LEN];
static SemaphoreHandle_t s_state_lock;
static esp_timer_handle_t s_reconnect_timer;

static void lock_state(void)
{
    if (s_state_lock != NULL) {
        xSemaphoreTake(s_state_lock, portMAX_DELAY);
    }
}

static void unlock_state(void)
{
    if (s_state_lock != NULL) {
        xSemaphoreGive(s_state_lock);
    }
}

static void set_last_error_locked(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    vsnprintf(s_last_error, sizeof(s_last_error), format, args);
    va_end(args);
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

static int compare_scan_results(const void *left, const void *right)
{
    const wifi_manager_scan_result_t *a =
        (const wifi_manager_scan_result_t *)left;
    const wifi_manager_scan_result_t *b =
        (const wifi_manager_scan_result_t *)right;

    if (a->rssi == b->rssi) {
        return 0;
    }
    return a->rssi > b->rssi ? -1 : 1;
}

static bool load_saved_station_config(char *ssid, char *password)
{
    config_service_wifi_t saved_wifi;
    bool found = false;
    esp_err_t err;

    ssid[0] = '\0';
    password[0] = '\0';

    err = config_service_load_wifi(&saved_wifi, &found);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "unable to load saved station Wi-Fi config: %s",
                 esp_err_to_name(err));
        return false;
    }
    if (!found) {
        return false;
    }

    copy_string(ssid, WIFI_MANAGER_SSID_MAX_LEN + 1, saved_wifi.ssid);
    copy_string(password, WIFI_MANAGER_PASSWORD_MAX_LEN + 1,
                saved_wifi.password);
    return true;
}

static esp_err_t save_station_config(void)
{
    config_service_wifi_t wifi;
    wifi_ap_record_t ap_info;
    char ssid[WIFI_MANAGER_SSID_MAX_LEN + 1];
    size_t configured_ssid_len;
    size_t connected_ssid_len;
    uint32_t generation;
    esp_err_t err;

    lock_state();
    if (!s_sta_configured || s_sta_saved) {
        unlock_state();
        return s_sta_configured ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    copy_string(wifi.ssid, sizeof(wifi.ssid), s_sta_ssid);
    copy_string(wifi.password, sizeof(wifi.password), s_sta_password);
    copy_string(ssid, sizeof(ssid), wifi.ssid);
    generation = s_sta_config_generation;
    unlock_state();

    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    configured_ssid_len = strnlen(wifi.ssid, sizeof(wifi.ssid));
    connected_ssid_len =
        strnlen((const char *)ap_info.ssid, sizeof(ap_info.ssid));
    if (configured_ssid_len != connected_ssid_len ||
        memcmp(wifi.ssid, ap_info.ssid, configured_ssid_len) != 0) {
        ESP_LOGW(TAG,
                 "connected SSID changed before persistence; not saving stale "
                 "configuration");
        return ESP_ERR_INVALID_STATE;
    }

    lock_state();
    if (!s_sta_configured || s_sta_saved ||
        generation != s_sta_config_generation ||
        strcmp(wifi.ssid, s_sta_ssid) != 0 ||
        strcmp(wifi.password, s_sta_password) != 0) {
        unlock_state();
        ESP_LOGW(TAG,
                 "station configuration changed before persistence; not "
                 "saving stale credentials");
        return ESP_ERR_INVALID_STATE;
    }

    err = config_service_save_wifi(&wifi);
    s_sta_saved = err == ESP_OK;
    if (err != ESP_OK) {
        set_last_error_locked("网络已连接，但配置保存失败");
    }
    unlock_state();

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "saved station Wi-Fi config for %s", ssid);
    } else {
        ESP_LOGE(TAG, "failed to save station Wi-Fi config: %s",
                 esp_err_to_name(err));
    }
    return err;
}

static void fail_station_attempt_locked(const char *message)
{
    s_sta_connecting = false;
    s_sta_connected = false;
    s_manual_attempt = false;
    s_reconfiguring = false;
    set_last_error_locked("%s", message);
}

static void fail_station_attempt(const char *message)
{
    lock_state();
    fail_station_attempt_locked(message);
    unlock_state();
}

static void reconnect_timer_callback(void *arg)
{
    bool should_connect;

    (void)arg;

    lock_state();
    should_connect = s_started && s_sta_configured && !s_manual_attempt;
    if (should_connect) {
        s_sta_connecting = true;
    }
    unlock_state();

    if (should_connect) {
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
            ESP_LOGW(TAG, "station reconnect request failed: %s",
                     esp_err_to_name(err));
            fail_station_attempt("自动重连失败");
        }
    }
}

static void schedule_reconnect(void)
{
    if (s_reconnect_timer == NULL) {
        return;
    }

    esp_timer_stop(s_reconnect_timer);
    esp_err_t err =
        esp_timer_start_once(s_reconnect_timer, WIFI_MANAGER_RECONNECT_DELAY_US);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "station reconnect scheduling failed: %s",
                 esp_err_to_name(err));
    }
}

static const char *disconnect_reason_message(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return "未找到该网络，请确认它在 2.4 GHz 频段";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        return "Wi-Fi 密码错误或认证失败";
    case WIFI_REASON_ASSOC_FAIL:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_CONNECTION_FAIL:
        return "无法加入该网络，请稍后重试";
    default:
        return NULL;
    }
}

static void log_connected_station(const wifi_event_ap_staconnected_t *event)
{
    ESP_LOGI(TAG, "station joined: " MACSTR ", aid=%d",
             MAC2STR(event->mac), event->aid);
}

static void log_disconnected_station(
    const wifi_event_ap_stadisconnected_t *event)
{
    ESP_LOGI(TAG, "station left: " MACSTR ", aid=%d",
             MAC2STR(event->mac), event->aid);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;

    switch (event_id) {
    case WIFI_EVENT_STA_START: {
        bool should_connect;

        lock_state();
        should_connect = s_sta_configured;
        if (should_connect) {
            s_sta_connecting = true;
        }
        unlock_state();

        if (should_connect) {
            esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
                ESP_LOGW(TAG, "station connect request failed: %s",
                         esp_err_to_name(err));
                fail_station_attempt("无法发起 Wi-Fi 连接");
            }
        }
        break;
    }
    case WIFI_EVENT_STA_DISCONNECTED: {
        const wifi_event_sta_disconnected_t *event =
            (const wifi_event_sta_disconnected_t *)event_data;
        const char *reason_message =
            disconnect_reason_message(event->reason);
        char error[WIFI_MANAGER_STATUS_ERROR_MAX_LEN];
        bool reconnect_now;
        bool retry_later;

        if (reason_message != NULL) {
            copy_string(error, sizeof(error), reason_message);
        } else {
            snprintf(error, sizeof(error), "Wi-Fi 连接中断（原因 %u）",
                     event->reason);
        }

        lock_state();
        s_sta_connected = false;
        copy_string(s_sta_ip, sizeof(s_sta_ip), "0.0.0.0");
        reconnect_now = s_reconfiguring;
        s_reconfiguring = false;
        if (reconnect_now) {
            s_sta_connecting = true;
            retry_later = false;
        } else if (s_manual_attempt) {
            fail_station_attempt_locked(error);
            retry_later = false;
        } else {
            s_sta_connecting = false;
            retry_later = s_started && s_sta_configured;
            set_last_error_locked("%s", error);
        }
        unlock_state();

        ESP_LOGW(TAG, "station disconnected from %s, reason=%u",
                 s_sta_ssid, event->reason);
        (void)app_events_publish(
            APP_EVENT_WIFI_STATION_DISCONNECTED, APP_EVENT_SOURCE_WIFI,
            APP_EVENT_SEVERITY_WARNING, event->reason, 0, 0);

        if (reconnect_now) {
            esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
                fail_station_attempt("重新连接 Wi-Fi 失败");
            }
        } else if (retry_later) {
            schedule_reconnect();
        }
        break;
    }
    case WIFI_EVENT_AP_STACONNECTED:
        lock_state();
        if (s_ap_client_count < UINT8_MAX) {
            s_ap_client_count++;
        }
        unlock_state();
        log_connected_station(
            (const wifi_event_ap_staconnected_t *)event_data);
        (void)app_events_publish(
            APP_EVENT_WIFI_CLIENT_CONNECTED, APP_EVENT_SOURCE_WIFI,
            APP_EVENT_SEVERITY_INFO, wifi_manager_ap_client_count(), 0, 0);
        break;
    case WIFI_EVENT_AP_STADISCONNECTED:
        lock_state();
        if (s_ap_client_count > 0) {
            s_ap_client_count--;
        }
        unlock_state();
        log_disconnected_station(
            (const wifi_event_ap_stadisconnected_t *)event_data);
        (void)app_events_publish(
            APP_EVENT_WIFI_CLIENT_DISCONNECTED, APP_EVENT_SOURCE_WIFI,
            APP_EVENT_SEVERITY_INFO, wifi_manager_ap_client_count(), 0, 0);
        break;
    default:
        break;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;

    if (event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        char ip[16];

        esp_ip4addr_ntoa(&event->ip_info.ip, ip, sizeof(ip));
        lock_state();
        s_sta_connected = true;
        s_sta_connecting = false;
        s_manual_attempt = false;
        s_reconfiguring = false;
        s_last_error[0] = '\0';
        copy_string(s_sta_ip, sizeof(s_sta_ip), ip);
        unlock_state();

        if (s_reconnect_timer != NULL) {
            esp_timer_stop(s_reconnect_timer);
        }

        ESP_LOGI(TAG, "station address: %s", ip);
        (void)app_events_publish(
            APP_EVENT_WIFI_STATION_CONNECTED, APP_EVENT_SOURCE_WIFI,
            APP_EVENT_SEVERITY_INFO, 0, 0, 0);
        save_station_config();
    } else if (event_id == IP_EVENT_STA_LOST_IP) {
        lock_state();
        s_sta_connected = false;
        copy_string(s_sta_ip, sizeof(s_sta_ip), "0.0.0.0");
        unlock_state();
        ESP_LOGW(TAG, "station address lost");
        (void)app_events_publish(
            APP_EVENT_WIFI_STATION_DISCONNECTED, APP_EVENT_SOURCE_WIFI,
            APP_EVENT_SEVERITY_WARNING, -1, 0, 0);
    }
}

static bool is_valid_config(const wifi_manager_config_t *config)
{
    size_t ap_password_len;

    if (config == NULL || config->ap_ssid == NULL || config->ap_ssid[0] == '\0') {
        return false;
    }
    if (strlen(config->ap_ssid) > WIFI_MANAGER_SSID_MAX_LEN) {
        return false;
    }

    ap_password_len =
        config->ap_password == NULL ? 0 : strlen(config->ap_password);
    if (ap_password_len != 0 &&
        (ap_password_len < 8 ||
         ap_password_len > WIFI_MANAGER_PASSWORD_MAX_LEN)) {
        return false;
    }

    if (config->sta_enabled) {
        if (config->sta_ssid == NULL || config->sta_ssid[0] == '\0' ||
            strlen(config->sta_ssid) > WIFI_MANAGER_SSID_MAX_LEN) {
            return false;
        }
        if (config->sta_password != NULL &&
            strlen(config->sta_password) >
                WIFI_MANAGER_PASSWORD_MAX_LEN) {
            return false;
        }
    }

    return config->ap_channel >= 1 && config->ap_channel <= 13 &&
           config->ap_max_clients >= 1 && config->ap_max_clients <= 10;
}

static esp_err_t set_ap_config(void)
{
    wifi_config_t ap_config = {0};
    size_t password_len = strlen(s_ap_password);

    memcpy(ap_config.ap.ssid, s_ap_ssid, strlen(s_ap_ssid));
    ap_config.ap.ssid_len = strlen(s_ap_ssid);
    ap_config.ap.channel = s_ap_channel;
    ap_config.ap.max_connection = s_ap_max_clients;
    ap_config.ap.authmode =
        password_len == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    if (password_len > 0) {
        memcpy(ap_config.ap.password, s_ap_password, password_len);
    }

    return esp_wifi_set_config(WIFI_IF_AP, &ap_config);
}

static esp_err_t set_sta_config(void)
{
    wifi_config_t sta_config = {0};
    char ssid[WIFI_MANAGER_SSID_MAX_LEN + 1];
    char password[WIFI_MANAGER_PASSWORD_MAX_LEN + 1];

    lock_state();
    if (!s_sta_configured) {
        unlock_state();
        return ESP_OK;
    }
    copy_string(ssid, sizeof(ssid), s_sta_ssid);
    copy_string(password, sizeof(password), s_sta_password);
    unlock_state();

    memcpy(sta_config.sta.ssid, ssid, strlen(ssid));
    memcpy(sta_config.sta.password, password, strlen(password));
    return esp_wifi_set_config(WIFI_IF_STA, &sta_config);
}

esp_err_t wifi_manager_init(const wifi_manager_config_t *config)
{
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    const esp_timer_create_args_t reconnect_timer_args = {
        .callback = reconnect_timer_callback,
        .name = "wifi_reconnect",
    };
    char saved_ssid[WIFI_MANAGER_SSID_MAX_LEN + 1];
    char saved_password[WIFI_MANAGER_PASSWORD_MAX_LEN + 1];

    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!is_valid_config(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    s_state_lock = xSemaphoreCreateMutex();
    if (s_state_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    copy_string(s_ap_ssid, sizeof(s_ap_ssid), config->ap_ssid);
    copy_string(s_ap_password, sizeof(s_ap_password), config->ap_password);
    s_ap_channel = config->ap_channel;
    s_ap_max_clients = config->ap_max_clients;

    if (load_saved_station_config(saved_ssid, saved_password)) {
        copy_string(s_sta_ssid, sizeof(s_sta_ssid), saved_ssid);
        copy_string(s_sta_password, sizeof(s_sta_password), saved_password);
        s_sta_configured = true;
        s_sta_saved = true;
        ESP_LOGI(TAG, "loaded saved station Wi-Fi config for %s", saved_ssid);
    } else if (config->sta_enabled) {
        copy_string(s_sta_ssid, sizeof(s_sta_ssid), config->sta_ssid);
        copy_string(s_sta_password, sizeof(s_sta_password),
                    config->sta_password);
        s_sta_configured = true;
        s_sta_saved = false;
        ESP_LOGI(TAG, "using station Wi-Fi config from build-time defaults");
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "network stack init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG,
                        "event loop init failed");

    if (esp_netif_create_default_wifi_ap() == NULL ||
        esp_netif_create_default_wifi_sta() == NULL) {
        return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "Wi-Fi init failed");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                   &wifi_event_handler, NULL),
        TAG, "Wi-Fi event handler registration failed");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                   &ip_event_handler, NULL),
        TAG, "IP event handler registration failed");
    ESP_RETURN_ON_ERROR(
        esp_timer_create(&reconnect_timer_args, &s_reconnect_timer),
        TAG, "reconnect timer creation failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG,
                        "Wi-Fi storage configuration failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG,
                        "Wi-Fi mode configuration failed");

    ESP_RETURN_ON_ERROR(set_ap_config(), TAG, "SoftAP configuration failed");
    ESP_RETURN_ON_ERROR(set_sta_config(), TAG, "station configuration failed");

    s_initialized = true;
    ESP_LOGI(TAG, "initialized in %s mode", wifi_manager_mode_name());
    return ESP_OK;
}

esp_err_t wifi_manager_start(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_started) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Wi-Fi start failed");
    s_started = true;
    ESP_LOGI(TAG, "SoftAP started: SSID=%s, channel=%d",
             s_ap_ssid, s_ap_channel);
    return ESP_OK;
}

esp_err_t wifi_manager_connect_station(const char *ssid,
                                       const char *password)
{
    size_t ssid_len;
    size_t password_len;
    esp_err_t err;

    if (!s_initialized || !s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ssid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ssid_len = strlen(ssid);
    password = password == NULL ? "" : password;
    password_len = strlen(password);
    if (ssid_len == 0 || ssid_len > WIFI_MANAGER_SSID_MAX_LEN ||
        password_len > WIFI_MANAGER_PASSWORD_MAX_LEN ||
        (password_len > 0 && password_len < 8)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_reconnect_timer != NULL) {
        esp_timer_stop(s_reconnect_timer);
    }

    lock_state();
    copy_string(s_sta_ssid, sizeof(s_sta_ssid), ssid);
    copy_string(s_sta_password, sizeof(s_sta_password), password);
    s_sta_configured = true;
    s_sta_saved = false;
    s_sta_connecting = true;
    s_sta_connected = false;
    s_manual_attempt = true;
    s_reconfiguring = true;
    s_sta_config_generation++;
    s_last_error[0] = '\0';
    unlock_state();

    err = set_sta_config();
    if (err != ESP_OK) {
        fail_station_attempt("无法应用 Wi-Fi 配置");
        return err;
    }

    err = esp_wifi_disconnect();
    if (err == ESP_ERR_WIFI_NOT_CONNECT) {
        lock_state();
        s_reconfiguring = false;
        unlock_state();
        err = esp_wifi_connect();
    }
    if (err != ESP_OK) {
        fail_station_attempt("无法发起 Wi-Fi 连接");
        return err;
    }

    ESP_LOGI(TAG, "station connection requested for %s", ssid);
    return ESP_OK;
}

esp_err_t wifi_manager_scan(wifi_manager_scan_result_t *results,
                            size_t max_results, size_t *result_count)
{
    wifi_scan_config_t scan_config = {
        .show_hidden = false,
    };
    wifi_ap_record_t *records;
    uint16_t record_count = 0;
    uint16_t requested_records;
    size_t count = 0;
    esp_err_t err;

    if (!s_initialized || !s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (results == NULL || max_results == 0 || result_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *result_count = 0;

    err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_scan_get_ap_num(&record_count);
    if (err != ESP_OK || record_count == 0) {
        esp_wifi_clear_ap_list();
        return err;
    }

    requested_records =
        record_count > WIFI_MANAGER_SCAN_MAX_RESULTS
            ? WIFI_MANAGER_SCAN_MAX_RESULTS
            : record_count;
    records = calloc(requested_records, sizeof(*records));
    if (records == NULL) {
        esp_wifi_clear_ap_list();
        return ESP_ERR_NO_MEM;
    }

    err = esp_wifi_scan_get_ap_records(&requested_records, records);
    if (err != ESP_OK) {
        free(records);
        esp_wifi_clear_ap_list();
        return err;
    }

    for (uint16_t index = 0;
         index < requested_records && count < max_results;
         index++) {
        bool duplicate = false;

        if (records[index].ssid[0] == '\0') {
            continue;
        }
        for (size_t existing = 0; existing < count; existing++) {
            if (strcmp(results[existing].ssid,
                       (const char *)records[index].ssid) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }

        copy_string(results[count].ssid, sizeof(results[count].ssid),
                    (const char *)records[index].ssid);
        results[count].rssi = records[index].rssi;
        results[count].secure =
            records[index].authmode != WIFI_AUTH_OPEN;
        count++;
    }

    free(records);
    qsort(results, count, sizeof(*results), compare_scan_results);
    *result_count = count;
    return ESP_OK;
}

bool wifi_manager_sta_connected(void)
{
    bool connected;

    lock_state();
    connected = s_sta_connected;
    unlock_state();
    return connected;
}

uint8_t wifi_manager_ap_client_count(void)
{
    uint8_t count;

    lock_state();
    count = s_ap_client_count;
    unlock_state();
    return count;
}

const char *wifi_manager_mode_name(void)
{
    return "STA+SoftAP";
}

void wifi_manager_get_status(wifi_manager_status_t *status)
{
    wifi_ap_record_t ap_info;

    if (status == NULL) {
        return;
    }

    memset(status, 0, sizeof(*status));
    status->rssi = -127;

    lock_state();
    status->configured = s_sta_configured;
    status->saved = s_sta_saved;
    status->connecting = s_sta_connecting;
    status->connected = s_sta_connected;
    copy_string(status->ssid, sizeof(status->ssid), s_sta_ssid);
    copy_string(status->ip, sizeof(status->ip), s_sta_ip);
    copy_string(status->last_error, sizeof(status->last_error),
                s_last_error);
    unlock_state();

    if (status->connected &&
        esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        status->rssi = ap_info.rssi;
    }
}
