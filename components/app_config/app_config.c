#include "app_config.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "sdkconfig.h"

#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASSWORD_MAX_LEN 64

static const char *TAG = "app_config";

#ifdef CONFIG_APP_WIFI_STA_ENABLED
#define APP_STA_ENABLED true
#define APP_STA_SSID CONFIG_APP_WIFI_STA_SSID
#define APP_STA_PASSWORD CONFIG_APP_WIFI_STA_PASSWORD
#else
#define APP_STA_ENABLED false
#define APP_STA_SSID ""
#define APP_STA_PASSWORD ""
#endif

#ifdef CONFIG_APP_HTTP_SERVER_ENABLED
#define APP_HTTP_SERVER_ENABLED true
#else
#define APP_HTTP_SERVER_ENABLED false
#endif

#ifdef CONFIG_APP_BOARD_ENABLED
#define APP_BOARD_ENABLED true
#define APP_OLED_I2C_PORT CONFIG_APP_OLED_I2C_PORT
#define APP_OLED_SDA_GPIO CONFIG_APP_OLED_I2C_SDA_GPIO
#define APP_OLED_SCL_GPIO CONFIG_APP_OLED_I2C_SCL_GPIO
#define APP_OLED_I2C_HZ CONFIG_APP_OLED_I2C_FREQ_HZ
#define APP_OLED_CONTROLLER \
    (CONFIG_APP_OLED_CONTROLLER_SH1106 \
         ? APP_OLED_CONTROLLER_SH1106 \
         : APP_OLED_CONTROLLER_SSD1306)
#define APP_BUTTON_K1_GPIO CONFIG_APP_BUTTON_K1_GPIO
#define APP_BUTTON_K2_GPIO CONFIG_APP_BUTTON_K2_GPIO
#define APP_BUTTON_K3_GPIO CONFIG_APP_BUTTON_K3_GPIO
#define APP_BUTTON_K4_GPIO CONFIG_APP_BUTTON_K4_GPIO
#define APP_BUTTON_ACTIVE_LOW true
#else
#define APP_BOARD_ENABLED false
#define APP_OLED_I2C_PORT 0
#define APP_OLED_SDA_GPIO 21
#define APP_OLED_SCL_GPIO 22
#define APP_OLED_I2C_HZ 100000u
#define APP_OLED_CONTROLLER APP_OLED_CONTROLLER_SH1106
#define APP_BUTTON_K1_GPIO 27
#define APP_BUTTON_K2_GPIO 16
#define APP_BUTTON_K3_GPIO 17
#define APP_BUTTON_K4_GPIO 14
#define APP_BUTTON_ACTIVE_LOW true
#endif

#ifdef CONFIG_APP_BUZZER_ENABLED
#define APP_BUZZER_ENABLED true
#define APP_BUZZER_GPIO CONFIG_APP_BUZZER_GPIO
#else
#define APP_BUZZER_ENABLED false
#define APP_BUZZER_GPIO 0
#endif

#ifdef CONFIG_APP_VIBRATION_ENABLED
#define APP_VIBRATION_ENABLED true
#define APP_VIBRATION_GPIO CONFIG_APP_VIBRATION_GPIO
#else
#define APP_VIBRATION_ENABLED false
#define APP_VIBRATION_GPIO 0
#endif

#ifdef CONFIG_APP_RGB_LED_ENABLED
#define APP_RGB_LED_ENABLED true
#define APP_RGB_LED_RED_GPIO CONFIG_APP_RGB_LED_RED_GPIO
#define APP_RGB_LED_GREEN_GPIO CONFIG_APP_RGB_LED_GREEN_GPIO
#define APP_RGB_LED_BLUE_GPIO CONFIG_APP_RGB_LED_BLUE_GPIO
#else
#define APP_RGB_LED_ENABLED false
#define APP_RGB_LED_RED_GPIO 0
#define APP_RGB_LED_GREEN_GPIO 0
#define APP_RGB_LED_BLUE_GPIO 0
#endif

#ifdef CONFIG_APP_STATUS_LED_ENABLED
#define APP_STATUS_LED_ENABLED true
#define APP_STATUS_LED_GPIO CONFIG_APP_STATUS_LED_GPIO
#ifdef CONFIG_APP_STATUS_LED_ACTIVE_HIGH
#define APP_STATUS_LED_ACTIVE_HIGH true
#else
#define APP_STATUS_LED_ACTIVE_HIGH false
#endif
#else
#define APP_STATUS_LED_ENABLED false
#define APP_STATUS_LED_GPIO 2
#define APP_STATUS_LED_ACTIVE_HIGH true
#endif

#ifdef APP_WIFI_INI_EMBEDDED
extern const uint8_t wifi_ini_start[] asm("_binary_wifi_ini_start");
extern const uint8_t wifi_ini_end[] asm("_binary_wifi_ini_end");

static char s_wifi_ini_ssid[WIFI_SSID_MAX_LEN + 1];
static char s_wifi_ini_password[WIFI_PASSWORD_MAX_LEN + 1];

static char *trim_whitespace(char *text)
{
    char *end;

    while (*text != '\0' && isspace((unsigned char)*text)) {
        text++;
    }

    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
    return text;
}

static bool copy_ini_value(char *destination, size_t destination_size,
                           char *value)
{
    char *trimmed = trim_whitespace(value);
    size_t length = strlen(trimmed);

    if (length >= 2 &&
        (trimmed[0] == '"' || trimmed[0] == '\'') &&
        trimmed[length - 1] == trimmed[0]) {
        trimmed[length - 1] = '\0';
        trimmed++;
        length -= 2;
    }

    if (length >= destination_size) {
        return false;
    }

    memcpy(destination, trimmed, length + 1);
    return true;
}

static bool parse_embedded_wifi_ini(void)
{
    const size_t size = (size_t)(wifi_ini_end - wifi_ini_start);
    char ssid[WIFI_SSID_MAX_LEN + 1] = {0};
    char password[WIFI_PASSWORD_MAX_LEN + 1] = {0};
    char *contents;
    char *line;
    char *save_pointer = NULL;
    bool ssid_seen = false;
    bool password_seen = false;
    bool valid = true;

    if (size == 0) {
        return false;
    }

    contents = malloc(size + 1);
    if (contents == NULL) {
        ESP_LOGW(TAG, "unable to allocate memory for wifi.ini");
        return false;
    }

    memcpy(contents, wifi_ini_start, size);
    contents[size] = '\0';

    for (line = strtok_r(contents, "\r\n", &save_pointer);
         line != NULL;
         line = strtok_r(NULL, "\r\n", &save_pointer)) {
        char *entry = trim_whitespace(line);
        char *separator;
        char *key;
        char *value;

        if (*entry == '\0' || *entry == '#' || *entry == ';') {
            continue;
        }

        separator = strchr(entry, ':');
        if (separator == NULL) {
            continue;
        }

        *separator = '\0';
        key = trim_whitespace(entry);
        value = separator + 1;

        if (strcmp(key, "wifi-name") == 0) {
            ssid_seen = true;
            valid = copy_ini_value(ssid, sizeof(ssid), value) && valid;
        } else if (strcmp(key, "wifi-password") == 0) {
            password_seen = true;
            valid = copy_ini_value(password, sizeof(password), value) && valid;
        }
    }

    free(contents);

    if (!valid || !ssid_seen || ssid[0] == '\0') {
        ESP_LOGW(TAG, "wifi.ini is invalid or has no wifi-name; using build-time station settings");
        return false;
    }
    if (!password_seen) {
        password[0] = '\0';
    }
    if (password[0] != '\0' && strlen(password) < 8) {
        ESP_LOGW(TAG, "wifi.ini password is too short; using build-time station settings");
        return false;
    }

    memcpy(s_wifi_ini_ssid, ssid, strlen(ssid) + 1);
    memcpy(s_wifi_ini_password, password, strlen(password) + 1);
    return true;
}
#endif

static app_config_t s_app_config = {
    .device_name = CONFIG_APP_DEVICE_NAME,
    .version = APP_VERSION,
    .http_server_enabled = APP_HTTP_SERVER_ENABLED,
    .wifi = {
        .ap_ssid = CONFIG_APP_WIFI_AP_SSID,
        .ap_password = CONFIG_APP_WIFI_AP_PASSWORD,
        .ap_channel = CONFIG_APP_WIFI_AP_CHANNEL,
        .ap_max_clients = 4,
        .sta_enabled = APP_STA_ENABLED,
        .sta_ssid = APP_STA_SSID,
        .sta_password = APP_STA_PASSWORD,
    },
    .board = {
        .enabled = APP_BOARD_ENABLED,
        .oled_i2c_port = APP_OLED_I2C_PORT,
        .oled_sda_gpio = APP_OLED_SDA_GPIO,
        .oled_scl_gpio = APP_OLED_SCL_GPIO,
        .oled_i2c_hz = APP_OLED_I2C_HZ,
        .oled_controller = APP_OLED_CONTROLLER,
        .button_gpio = {
            APP_BUTTON_K1_GPIO,
            APP_BUTTON_K2_GPIO,
            APP_BUTTON_K3_GPIO,
            APP_BUTTON_K4_GPIO,
        },
        .button_active_low = APP_BUTTON_ACTIVE_LOW,
        .buzzer_enabled = APP_BUZZER_ENABLED,
        .buzzer_gpio = APP_BUZZER_GPIO,
        .vibration_enabled = APP_VIBRATION_ENABLED,
        .vibration_gpio = APP_VIBRATION_GPIO,
        .status_led_enabled = APP_STATUS_LED_ENABLED,
        .status_led_gpio = APP_STATUS_LED_GPIO,
        .status_led_active_high = APP_STATUS_LED_ACTIVE_HIGH,
        .rgb_led_enabled = APP_RGB_LED_ENABLED,
        .rgb_red_gpio = APP_RGB_LED_RED_GPIO,
        .rgb_green_gpio = APP_RGB_LED_GREEN_GPIO,
        .rgb_blue_gpio = APP_RGB_LED_BLUE_GPIO,
    },
};

static bool s_wifi_config_loaded;

const app_config_t *app_config_get(void)
{
    if (!s_wifi_config_loaded) {
        s_wifi_config_loaded = true;
#ifdef APP_WIFI_INI_EMBEDDED
        if (parse_embedded_wifi_ini()) {
            s_app_config.wifi.sta_enabled = true;
            s_app_config.wifi.sta_ssid = s_wifi_ini_ssid;
            s_app_config.wifi.sta_password = s_wifi_ini_password;
            ESP_LOGI(TAG, "loaded station Wi-Fi settings from embedded wifi.ini");
        }
#endif
    }

    return &s_app_config;
}
