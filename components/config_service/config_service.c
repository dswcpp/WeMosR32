#include "config_service.h"

#include <stddef.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"
#include "nvs_flash.h"

#define CONFIG_SERVICE_NAMESPACE "wifi_cfg"
#define CONFIG_SERVICE_KEY_SSID "ssid"
#define CONFIG_SERVICE_KEY_PASSWORD "password"
#define CONFIG_SERVICE_KEY_WIFI_RECORD "config"
#define CONFIG_SERVICE_WIFI_RECORD_MAGIC 0x57494631u
#define CONFIG_SERVICE_WIFI_RECORD_VERSION 1u

static const char *TAG = "config_service";
static bool s_initialized;
static bool s_recovery_required;
static esp_err_t s_last_error;
static portMUX_TYPE s_status_mux = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    const char *namespace_name;
    const char *key;
    size_t max_size;
} config_service_blob_spec_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    char ssid[CONFIG_SERVICE_WIFI_SSID_MAX_LEN + 1];
    char password[CONFIG_SERVICE_WIFI_PASSWORD_MAX_LEN + 1];
} config_service_wifi_record_t;

static const config_service_blob_spec_t s_blob_specs[] = {
    [CONFIG_SERVICE_ITEM_CARE_SETTINGS] = {
        .namespace_name = "wellness_cfg",
        .key = "settings",
        .max_size = CONFIG_SERVICE_BLOB_MAX_CARE_SETTINGS,
    },
    [CONFIG_SERVICE_ITEM_CARE_DAILY] = {
        .namespace_name = "wellness_cfg",
        .key = "daily",
        .max_size = CONFIG_SERVICE_BLOB_MAX_CARE_DAILY,
    },
    [CONFIG_SERVICE_ITEM_RGB_SETTINGS] = {
        .namespace_name = "wellness_cfg",
        .key = "rgb",
        .max_size = CONFIG_SERVICE_BLOB_MAX_RGB_SETTINGS,
    },
    [CONFIG_SERVICE_ITEM_WEATHER_SETTINGS] = {
        .namespace_name = "weather_cfg",
        .key = "settings",
        .max_size = CONFIG_SERVICE_BLOB_MAX_WEATHER_SETTINGS,
    },
    [CONFIG_SERVICE_ITEM_WEATHER_CACHE] = {
        .namespace_name = "weather_cfg",
        .key = "cache",
        .max_size = CONFIG_SERVICE_BLOB_MAX_WEATHER_CACHE,
    },
    [CONFIG_SERVICE_ITEM_WEB_SETTINGS] = {
        .namespace_name = "web_cfg",
        .key = "pages",
        .max_size = CONFIG_SERVICE_BLOB_MAX_WEB_SETTINGS,
    },
};

static void record_result(esp_err_t err)
{
    taskENTER_CRITICAL(&s_status_mux);
    s_last_error = err;
    taskEXIT_CRITICAL(&s_status_mux);
}

static const config_service_blob_spec_t *get_blob_spec(
    config_service_item_t item)
{
    size_t item_index = (size_t)item;

    if (item_index >= sizeof(s_blob_specs) / sizeof(s_blob_specs[0])) {
        return NULL;
    }
    return &s_blob_specs[item_index];
}

static bool is_valid_wifi_config(const config_service_wifi_t *wifi)
{
    size_t ssid_len;
    size_t password_len;

    if (wifi == NULL) {
        return false;
    }

    ssid_len = strnlen(wifi->ssid, sizeof(wifi->ssid));
    password_len = strnlen(wifi->password, sizeof(wifi->password));
    return ssid_len > 0 && ssid_len <= CONFIG_SERVICE_WIFI_SSID_MAX_LEN &&
           password_len <= CONFIG_SERVICE_WIFI_PASSWORD_MAX_LEN &&
           (password_len == 0 || password_len >= 8);
}

static bool is_valid_wifi_record(
    const config_service_wifi_record_t *record)
{
    config_service_wifi_t wifi;

    if (record == NULL ||
        record->magic != CONFIG_SERVICE_WIFI_RECORD_MAGIC ||
        record->version != CONFIG_SERVICE_WIFI_RECORD_VERSION) {
        return false;
    }

    memcpy(wifi.ssid, record->ssid, sizeof(wifi.ssid));
    memcpy(wifi.password, record->password, sizeof(wifi.password));
    wifi.ssid[sizeof(wifi.ssid) - 1u] = '\0';
    wifi.password[sizeof(wifi.password) - 1u] = '\0';
    return is_valid_wifi_config(&wifi);
}

esp_err_t config_service_init(void)
{
    esp_err_t err;

    if (s_initialized) {
        return ESP_OK;
    }

    err = nvs_flash_init();
    if (err != ESP_OK) {
        s_recovery_required =
            err == ESP_ERR_NVS_NO_FREE_PAGES ||
            err == ESP_ERR_NVS_NEW_VERSION_FOUND;
        record_result(err);
        ESP_LOGE(TAG, "NVS initialization failed: %s", esp_err_to_name(err));
        if (s_recovery_required) {
            ESP_LOGE(TAG,
                     "NVS recovery is required; automatic erase is disabled "
                     "to protect stored configuration");
        }
        return err;
    }

    s_initialized = true;
    s_recovery_required = false;
    record_result(ESP_OK);
    ESP_LOGI(TAG, "configuration storage ready");
    return ESP_OK;
}

bool config_service_is_initialized(void)
{
    return s_initialized;
}

esp_err_t config_service_get_status(config_service_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    taskENTER_CRITICAL(&s_status_mux);
    status->initialized = s_initialized;
    status->recovery_required = s_recovery_required;
    status->last_error = s_last_error;
    taskEXIT_CRITICAL(&s_status_mux);
    return ESP_OK;
}

esp_err_t config_service_factory_reset(void)
{
    esp_err_t err;

    if (s_initialized) {
        err = nvs_flash_deinit();
        if (err != ESP_OK) {
            record_result(err);
            return err;
        }
        s_initialized = false;
    }

    err = nvs_flash_erase();
    if (err == ESP_OK) {
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        s_recovery_required = true;
        record_result(err);
        ESP_LOGE(TAG, "NVS factory reset failed: %s", esp_err_to_name(err));
        return err;
    }

    s_initialized = true;
    s_recovery_required = false;
    record_result(ESP_OK);
    ESP_LOGW(TAG, "NVS factory reset completed");
    return ESP_OK;
}

esp_err_t config_service_load_wifi(config_service_wifi_t *wifi, bool *found)
{
    nvs_handle_t handle;
    config_service_wifi_record_t record;
    size_t record_size;
    size_t ssid_size;
    size_t password_size;
    esp_err_t err;

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (wifi == NULL || found == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(wifi, 0, sizeof(*wifi));
    *found = false;

    err = nvs_open(CONFIG_SERVICE_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        record_result(ESP_OK);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        record_result(err);
        ESP_LOGW(TAG, "failed to open Wi-Fi configuration: %s",
                 esp_err_to_name(err));
        return err;
    }

    record_size = sizeof(record);
    err = nvs_get_blob(handle, CONFIG_SERVICE_KEY_WIFI_RECORD, &record,
                       &record_size);
    if (err == ESP_OK) {
        nvs_close(handle);
        if (record_size != sizeof(record) || !is_valid_wifi_record(&record)) {
            record_result(ESP_ERR_INVALID_STATE);
            ESP_LOGW(TAG, "saved Wi-Fi record is invalid");
            memset(wifi, 0, sizeof(*wifi));
            return ESP_ERR_INVALID_STATE;
        }
        memcpy(wifi->ssid, record.ssid, sizeof(wifi->ssid));
        memcpy(wifi->password, record.password, sizeof(wifi->password));
        wifi->ssid[sizeof(wifi->ssid) - 1u] = '\0';
        wifi->password[sizeof(wifi->password) - 1u] = '\0';
        *found = true;
        record_result(ESP_OK);
        return ESP_OK;
    }
    if (err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        record_result(err);
        ESP_LOGW(TAG, "failed to read Wi-Fi record: %s",
                 esp_err_to_name(err));
        return err;
    }

    /* Load legacy SSID/password keys until the next successful save
       migrates them into one atomic record. */
    ssid_size = sizeof(wifi->ssid);
    err = nvs_get_str(handle, CONFIG_SERVICE_KEY_SSID, wifi->ssid, &ssid_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        record_result(ESP_OK);
        return ESP_OK;
    }
    if (err == ESP_OK) {
        password_size = sizeof(wifi->password);
        err = nvs_get_str(handle, CONFIG_SERVICE_KEY_PASSWORD,
                          wifi->password, &password_size);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            wifi->password[0] = '\0';
            err = ESP_OK;
        }
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        record_result(err);
        ESP_LOGW(TAG, "failed to read Wi-Fi configuration: %s",
                 esp_err_to_name(err));
        memset(wifi, 0, sizeof(*wifi));
        return err;
    }
    if (!is_valid_wifi_config(wifi)) {
        record_result(ESP_ERR_INVALID_STATE);
        ESP_LOGW(TAG, "saved Wi-Fi configuration is invalid");
        memset(wifi, 0, sizeof(*wifi));
        return ESP_ERR_INVALID_STATE;
    }

    *found = true;
    record_result(ESP_OK);
    return ESP_OK;
}

esp_err_t config_service_save_wifi(const config_service_wifi_t *wifi)
{
    config_service_wifi_record_t record = {
        .magic = CONFIG_SERVICE_WIFI_RECORD_MAGIC,
        .version = CONFIG_SERVICE_WIFI_RECORD_VERSION,
    };
    nvs_handle_t handle;
    esp_err_t err;

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!is_valid_wifi_config(wifi)) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(record.ssid, wifi->ssid, sizeof(record.ssid));
    memcpy(record.password, wifi->password, sizeof(record.password));
    record.ssid[sizeof(record.ssid) - 1u] = '\0';
    record.password[sizeof(record.password) - 1u] = '\0';

    err = nvs_open(CONFIG_SERVICE_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        record_result(err);
        return err;
    }

    err = nvs_set_blob(handle, CONFIG_SERVICE_KEY_WIFI_RECORD, &record,
                       sizeof(record));
    if (err == ESP_OK) {
        esp_err_t erase_err =
            nvs_erase_key(handle, CONFIG_SERVICE_KEY_SSID);
        if (erase_err != ESP_OK && erase_err != ESP_ERR_NVS_NOT_FOUND) {
            err = erase_err;
        }
    }
    if (err == ESP_OK) {
        esp_err_t erase_err =
            nvs_erase_key(handle, CONFIG_SERVICE_KEY_PASSWORD);
        if (erase_err != ESP_OK && erase_err != ESP_ERR_NVS_NOT_FOUND) {
            err = erase_err;
        }
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        record_result(ESP_OK);
        ESP_LOGI(TAG, "saved Wi-Fi configuration for %s", wifi->ssid);
    } else {
        record_result(err);
        ESP_LOGE(TAG, "failed to save Wi-Fi configuration: %s",
                 esp_err_to_name(err));
    }
    return err;
}

esp_err_t config_service_clear_wifi(void)
{
    nvs_handle_t handle;
    esp_err_t err;

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    err = nvs_open(CONFIG_SERVICE_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        record_result(ESP_OK);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        record_result(err);
        return err;
    }

    err = nvs_erase_key(handle, CONFIG_SERVICE_KEY_WIFI_RECORD);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        esp_err_t legacy_err =
            nvs_erase_key(handle, CONFIG_SERVICE_KEY_SSID);
        if (legacy_err != ESP_OK && legacy_err != ESP_ERR_NVS_NOT_FOUND) {
            err = legacy_err;
        }
    }
    if (err == ESP_OK) {
        esp_err_t legacy_err =
            nvs_erase_key(handle, CONFIG_SERVICE_KEY_PASSWORD);
        if (legacy_err != ESP_OK && legacy_err != ESP_ERR_NVS_NOT_FOUND) {
            err = legacy_err;
        }
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        record_result(ESP_OK);
        ESP_LOGI(TAG, "cleared Wi-Fi configuration");
    } else {
        record_result(err);
        ESP_LOGE(TAG, "failed to clear Wi-Fi configuration: %s",
                 esp_err_to_name(err));
    }
    return err;
}

esp_err_t config_service_load_blob(config_service_item_t item, void *data,
                                   size_t *size, bool *found)
{
    const config_service_blob_spec_t *spec = get_blob_spec(item);
    nvs_handle_t handle;
    esp_err_t err;

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (spec == NULL || data == NULL || size == NULL || *size == 0 ||
        found == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *found = false;
    err = nvs_open(spec->namespace_name, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        record_result(ESP_OK);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        record_result(err);
        return err;
    }

    err = nvs_get_blob(handle, spec->key, data, size);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        record_result(ESP_OK);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        record_result(err);
        ESP_LOGW(TAG, "failed to load %s/%s: %s", spec->namespace_name,
                 spec->key, esp_err_to_name(err));
        return err;
    }
    if (*size > spec->max_size) {
        record_result(ESP_ERR_INVALID_SIZE);
        ESP_LOGW(TAG, "stored blob %s/%s is too large: %u bytes",
                 spec->namespace_name, spec->key, (unsigned)*size);
        return ESP_ERR_INVALID_SIZE;
    }

    *found = true;
    record_result(ESP_OK);
    return ESP_OK;
}

esp_err_t config_service_save_blob(config_service_item_t item,
                                   const void *data, size_t size)
{
    const config_service_blob_spec_t *spec = get_blob_spec(item);
    nvs_handle_t handle;
    esp_err_t err;

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (spec == NULL || data == NULL || size == 0 ||
        size > spec->max_size) {
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_open(spec->namespace_name, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        record_result(err);
        return err;
    }

    err = nvs_set_blob(handle, spec->key, data, size);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        record_result(err);
        ESP_LOGE(TAG, "failed to save %s/%s: %s", spec->namespace_name,
                 spec->key, esp_err_to_name(err));
    } else {
        record_result(ESP_OK);
    }
    return err;
}

esp_err_t config_service_clear_blob(config_service_item_t item)
{
    const config_service_blob_spec_t *spec = get_blob_spec(item);
    nvs_handle_t handle;
    esp_err_t err;

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (spec == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_open(spec->namespace_name, NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        record_result(ESP_OK);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        record_result(err);
        return err;
    }

    err = nvs_erase_key(handle, spec->key);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    record_result(err);
    return err;
}

esp_err_t config_service_save_blob_and_clear_blob(
    config_service_item_t save_item, const void *data, size_t size,
    config_service_item_t clear_item)
{
    const config_service_blob_spec_t *save_spec =
        get_blob_spec(save_item);
    const config_service_blob_spec_t *clear_spec =
        get_blob_spec(clear_item);
    nvs_handle_t handle;
    esp_err_t err;

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (save_spec == NULL || clear_spec == NULL || data == NULL ||
        size == 0 || size > save_spec->max_size ||
        save_item == clear_item ||
        strcmp(save_spec->namespace_name, clear_spec->namespace_name) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_open(save_spec->namespace_name, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        record_result(err);
        return err;
    }

    err = nvs_set_blob(handle, save_spec->key, data, size);
    if (err == ESP_OK) {
        esp_err_t erase_err =
            nvs_erase_key(handle, clear_spec->key);

        if (erase_err != ESP_OK && erase_err != ESP_ERR_NVS_NOT_FOUND) {
            err = erase_err;
        }
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        record_result(err);
        ESP_LOGE(TAG, "failed to replace %s/%s and clear %s/%s: %s",
                 save_spec->namespace_name, save_spec->key,
                 clear_spec->namespace_name, clear_spec->key,
                 esp_err_to_name(err));
    } else {
        record_result(ESP_OK);
    }
    return err;
}
