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

#ifdef CONFIG_APP_WELLNESS_ENABLED
#define APP_WELLNESS_ENABLED true
#define APP_PIR_GPIO CONFIG_APP_PIR_GPIO
#define APP_SHT30_UART_PORT CONFIG_APP_SHT30_UART_PORT
#define APP_SHT30_TX_GPIO CONFIG_APP_SHT30_UART_TX_GPIO
#define APP_SHT30_RX_GPIO CONFIG_APP_SHT30_UART_RX_GPIO
#define APP_OLED_I2C_PORT CONFIG_APP_OLED_I2C_PORT
#define APP_OLED_SDA_GPIO CONFIG_APP_OLED_I2C_SDA_GPIO
#define APP_OLED_SCL_GPIO CONFIG_APP_OLED_I2C_SCL_GPIO
#define APP_OLED_I2C_HZ CONFIG_APP_OLED_I2C_FREQ_HZ
#ifdef CONFIG_APP_OLED_CONTROLLER_SH1106
#define APP_OLED_SH1106 true
#else
#define APP_OLED_SH1106 false
#endif
#define APP_SEDENTARY_INITIAL_MS \
    ((uint32_t)CONFIG_APP_SEDENTARY_INITIAL_MINUTES * 60u * 1000u)
#define APP_SEDENTARY_ESCALATION_MS \
    ((uint32_t)CONFIG_APP_SEDENTARY_ESCALATION_MINUTES * 60u * 1000u)
#define APP_SEDENTARY_BREAK_MS \
    ((uint32_t)CONFIG_APP_SEDENTARY_BREAK_MINUTES * 60u * 1000u)
#define APP_SEDENTARY_ABSENCE_MS \
    ((uint32_t)CONFIG_APP_SEDENTARY_ABSENCE_SECONDS * 1000u)
#else
#define APP_WELLNESS_ENABLED false
#define APP_PIR_GPIO 0
#define APP_SHT30_UART_PORT 2
#define APP_SHT30_TX_GPIO 17
#define APP_SHT30_RX_GPIO 16
#define APP_OLED_I2C_PORT 0
#define APP_OLED_SDA_GPIO 21
#define APP_OLED_SCL_GPIO 22
#define APP_OLED_I2C_HZ 100000u
#define APP_OLED_SH1106 true
#define APP_SEDENTARY_INITIAL_MS (45u * 60u * 1000u)
#define APP_SEDENTARY_ESCALATION_MS (5u * 60u * 1000u)
#define APP_SEDENTARY_BREAK_MS (3u * 60u * 1000u)
#define APP_SEDENTARY_ABSENCE_MS (30u * 1000u)
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

#ifdef CONFIG_APP_BUTTONS_ENABLED
#define APP_BUTTONS_ENABLED true
#define APP_BUTTON_ACK_GPIO CONFIG_APP_BUTTON_ACK_GPIO
#define APP_BUTTON_WATER_GPIO CONFIG_APP_BUTTON_WATER_GPIO
#define APP_BUTTON_MUTE_GPIO CONFIG_APP_BUTTON_MUTE_GPIO
#define APP_BUTTON_PAGE_GPIO CONFIG_APP_BUTTON_PAGE_GPIO
#define APP_BUTTON_DEBOUNCE_MS \
    ((uint32_t)CONFIG_APP_BUTTON_DEBOUNCE_MS)
#define APP_BUTTON_LONG_PRESS_MS \
    ((uint32_t)CONFIG_APP_BUTTON_LONG_PRESS_MS)
#else
#define APP_BUTTONS_ENABLED false
#define APP_BUTTON_ACK_GPIO 32
#define APP_BUTTON_WATER_GPIO 33
#define APP_BUTTON_MUTE_GPIO 34
#define APP_BUTTON_PAGE_GPIO 35
#define APP_BUTTON_DEBOUNCE_MS 25u
#define APP_BUTTON_LONG_PRESS_MS 1500u
#endif

#ifdef CONFIG_APP_WORKSTATION_LIGHT_ENABLED
#define APP_WORKSTATION_LIGHT_ENABLED true
#define APP_WORKSTATION_LIGHT_POWER_GPIO \
    CONFIG_APP_WORKSTATION_LIGHT_POWER_GPIO
#define APP_WORKSTATION_LIGHT_BRIGHTNESS_GPIO \
    CONFIG_APP_WORKSTATION_LIGHT_BRIGHTNESS_GPIO
#define APP_WORKSTATION_LIGHT_COLOR_GPIO \
    CONFIG_APP_WORKSTATION_LIGHT_COLOR_GPIO
#ifdef CONFIG_APP_WORKSTATION_LIGHT_RELAY_ACTIVE_LOW
#define APP_WORKSTATION_LIGHT_ACTIVE_LOW true
#else
#define APP_WORKSTATION_LIGHT_ACTIVE_LOW false
#endif
#define APP_WORKSTATION_LIGHT_PULSE_MS \
    ((uint32_t)CONFIG_APP_WORKSTATION_LIGHT_PULSE_MS)
#define APP_WORKSTATION_LIGHT_SETTLE_MS \
    ((uint32_t)CONFIG_APP_WORKSTATION_LIGHT_SETTLE_MS)
#ifdef CONFIG_APP_WORKSTATION_LIGHT_PRESENCE_AUTO
#define APP_WORKSTATION_LIGHT_PRESENCE_AUTO true
#else
#define APP_WORKSTATION_LIGHT_PRESENCE_AUTO false
#endif
#ifdef CONFIG_APP_WORKSTATION_LIGHT_PRESENCE_LED_ENABLED
#define APP_WORKSTATION_LIGHT_PRESENCE_LED_ENABLED true
#define APP_WORKSTATION_LIGHT_PRESENCE_LED_GPIO \
    CONFIG_APP_WORKSTATION_LIGHT_PRESENCE_LED_GPIO
#ifdef CONFIG_APP_WORKSTATION_LIGHT_PRESENCE_LED_ACTIVE_LOW
#define APP_WORKSTATION_LIGHT_PRESENCE_LED_ACTIVE_LOW true
#else
#define APP_WORKSTATION_LIGHT_PRESENCE_LED_ACTIVE_LOW false
#endif
#else
#define APP_WORKSTATION_LIGHT_PRESENCE_LED_ENABLED false
#define APP_WORKSTATION_LIGHT_PRESENCE_LED_GPIO -1
#define APP_WORKSTATION_LIGHT_PRESENCE_LED_ACTIVE_LOW false
#endif
#else
#define APP_WORKSTATION_LIGHT_ENABLED false
#define APP_WORKSTATION_LIGHT_POWER_GPIO 0
#define APP_WORKSTATION_LIGHT_BRIGHTNESS_GPIO 0
#define APP_WORKSTATION_LIGHT_COLOR_GPIO 0
#define APP_WORKSTATION_LIGHT_ACTIVE_LOW false
#define APP_WORKSTATION_LIGHT_PULSE_MS 180u
#define APP_WORKSTATION_LIGHT_SETTLE_MS 160u
#define APP_WORKSTATION_LIGHT_PRESENCE_AUTO false
#define APP_WORKSTATION_LIGHT_PRESENCE_LED_ENABLED false
#define APP_WORKSTATION_LIGHT_PRESENCE_LED_GPIO -1
#define APP_WORKSTATION_LIGHT_PRESENCE_LED_ACTIVE_LOW false
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
    .wellness = {
        .enabled = APP_WELLNESS_ENABLED,
        .pir_gpio = APP_PIR_GPIO,
        .sht30_uart_port = APP_SHT30_UART_PORT,
        .sht30_tx_gpio = APP_SHT30_TX_GPIO,
        .sht30_rx_gpio = APP_SHT30_RX_GPIO,
        .oled_i2c_port = APP_OLED_I2C_PORT,
        .oled_sda_gpio = APP_OLED_SDA_GPIO,
        .oled_scl_gpio = APP_OLED_SCL_GPIO,
        .oled_i2c_hz = APP_OLED_I2C_HZ,
        .oled_sh1106 = APP_OLED_SH1106,
        .buzzer_enabled = APP_BUZZER_ENABLED,
        .buzzer_gpio = APP_BUZZER_GPIO,
        .vibration_enabled = APP_VIBRATION_ENABLED,
        .vibration_gpio = APP_VIBRATION_GPIO,
        .rgb_led_enabled = APP_RGB_LED_ENABLED,
        .rgb_red_gpio = APP_RGB_LED_RED_GPIO,
        .rgb_green_gpio = APP_RGB_LED_GREEN_GPIO,
        .rgb_blue_gpio = APP_RGB_LED_BLUE_GPIO,
        .buttons_enabled = APP_BUTTONS_ENABLED,
        .button_gpios = {
            APP_BUTTON_ACK_GPIO,
            APP_BUTTON_WATER_GPIO,
            APP_BUTTON_MUTE_GPIO,
            APP_BUTTON_PAGE_GPIO,
        },
        .button_debounce_ms = APP_BUTTON_DEBOUNCE_MS,
        .button_long_press_ms = APP_BUTTON_LONG_PRESS_MS,
        .sedentary_initial_ms = APP_SEDENTARY_INITIAL_MS,
        .sedentary_escalation_ms = APP_SEDENTARY_ESCALATION_MS,
        .sedentary_break_ms = APP_SEDENTARY_BREAK_MS,
        .sedentary_absence_ms = APP_SEDENTARY_ABSENCE_MS,
    },
    .workstation_light = {
        .enabled = APP_WORKSTATION_LIGHT_ENABLED,
        .power_gpio = APP_WORKSTATION_LIGHT_POWER_GPIO,
        .brightness_gpio = APP_WORKSTATION_LIGHT_BRIGHTNESS_GPIO,
        .color_gpio = APP_WORKSTATION_LIGHT_COLOR_GPIO,
        .active_low = APP_WORKSTATION_LIGHT_ACTIVE_LOW,
        .pulse_ms = APP_WORKSTATION_LIGHT_PULSE_MS,
        .settle_ms = APP_WORKSTATION_LIGHT_SETTLE_MS,
        .presence_auto = APP_WORKSTATION_LIGHT_PRESENCE_AUTO,
        .presence_led_enabled =
            APP_WORKSTATION_LIGHT_PRESENCE_LED_ENABLED,
        .presence_led_gpio = APP_WORKSTATION_LIGHT_PRESENCE_LED_GPIO,
        .presence_led_active_low =
            APP_WORKSTATION_LIGHT_PRESENCE_LED_ACTIVE_LOW,
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
