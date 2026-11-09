#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *ap_ssid;
    const char *ap_password;
    uint8_t ap_channel;
    uint8_t ap_max_clients;
    bool sta_enabled;
    const char *sta_ssid;
    const char *sta_password;
} wifi_manager_config_t;

#define WIFI_MANAGER_SSID_MAX_LEN 32
#define WIFI_MANAGER_PASSWORD_MAX_LEN 64
#define WIFI_MANAGER_STATUS_ERROR_MAX_LEN 72
#define WIFI_MANAGER_SCAN_MAX_RESULTS 16

typedef struct {
    bool configured;
    bool saved;
    bool connecting;
    bool connected;
    char ssid[WIFI_MANAGER_SSID_MAX_LEN + 1];
    char ip[16];
    int8_t rssi;
    char last_error[WIFI_MANAGER_STATUS_ERROR_MAX_LEN];
} wifi_manager_status_t;

typedef struct {
    char ssid[WIFI_MANAGER_SSID_MAX_LEN + 1];
    int8_t rssi;
    bool secure;
} wifi_manager_scan_result_t;

esp_err_t wifi_manager_init(const wifi_manager_config_t *config);
esp_err_t wifi_manager_start(void);
esp_err_t wifi_manager_connect_station(const char *ssid, const char *password);
esp_err_t wifi_manager_scan(wifi_manager_scan_result_t *results,
                            size_t max_results, size_t *result_count);

bool wifi_manager_sta_connected(void);
uint8_t wifi_manager_ap_client_count(void);
const char *wifi_manager_mode_name(void);
void wifi_manager_get_status(wifi_manager_status_t *status);

#ifdef __cplusplus
}
#endif

