#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIG_SERVICE_WIFI_SSID_MAX_LEN 32
#define CONFIG_SERVICE_WIFI_PASSWORD_MAX_LEN 64

typedef struct {
    char ssid[CONFIG_SERVICE_WIFI_SSID_MAX_LEN + 1];
    char password[CONFIG_SERVICE_WIFI_PASSWORD_MAX_LEN + 1];
} config_service_wifi_t;

typedef enum {
    CONFIG_SERVICE_ITEM_CARE_SETTINGS = 0,
    CONFIG_SERVICE_ITEM_CARE_DAILY,
    CONFIG_SERVICE_ITEM_RGB_SETTINGS,
    CONFIG_SERVICE_ITEM_WEATHER_SETTINGS,
    CONFIG_SERVICE_ITEM_WEATHER_CACHE,
    CONFIG_SERVICE_ITEM_WEB_SETTINGS,
} config_service_item_t;

typedef struct {
    bool initialized;
    bool recovery_required;
    esp_err_t last_error;
} config_service_status_t;

esp_err_t config_service_init(void);
bool config_service_is_initialized(void);
esp_err_t config_service_get_status(config_service_status_t *status);
esp_err_t config_service_factory_reset(void);

esp_err_t config_service_load_wifi(config_service_wifi_t *wifi, bool *found);
esp_err_t config_service_save_wifi(const config_service_wifi_t *wifi);
esp_err_t config_service_clear_wifi(void);

esp_err_t config_service_load_blob(config_service_item_t item, void *data,
                                   size_t *size, bool *found);
esp_err_t config_service_save_blob(config_service_item_t item,
                                   const void *data, size_t size);
esp_err_t config_service_clear_blob(config_service_item_t item);
esp_err_t config_service_save_blob_and_clear_blob(
    config_service_item_t save_item, const void *data, size_t size,
    config_service_item_t clear_item);

#ifdef __cplusplus
}
#endif
