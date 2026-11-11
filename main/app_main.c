#include "app_config.h"
#include "app_events.h"
#include "button_service.h"
#include "config_service.h"
#include <stddef.h>

#include "esp_chip_info.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "feedback_service.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_api.h"
#include "rgb_indicator.h"
#include "startup_service.h"
#include "status_led.h"
#include "stock_monitor.h"
#include "stock_service.h"
#include "time_service.h"
#include "wifi_manager.h"

static const char *TAG = "app";

#define HTTP_API_START_ATTEMPTS 3u
#define HTTP_API_START_RETRY_DELAY_MS 250u

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static startup_reset_reason_t reset_reason_from_esp(
    esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON:
        return STARTUP_RESET_POWER_ON;
    case ESP_RST_EXT:
        return STARTUP_RESET_EXTERNAL;
    case ESP_RST_SW:
        return STARTUP_RESET_SOFTWARE;
    case ESP_RST_PANIC:
        return STARTUP_RESET_PANIC;
    case ESP_RST_INT_WDT:
        return STARTUP_RESET_INTERRUPT_WATCHDOG;
    case ESP_RST_TASK_WDT:
        return STARTUP_RESET_TASK_WATCHDOG;
    case ESP_RST_WDT:
        return STARTUP_RESET_WATCHDOG;
    case ESP_RST_DEEPSLEEP:
        return STARTUP_RESET_DEEP_SLEEP;
    case ESP_RST_BROWNOUT:
        return STARTUP_RESET_BROWNOUT;
    case ESP_RST_SDIO:
        return STARTUP_RESET_SDIO;
    case ESP_RST_USB:
        return STARTUP_RESET_USB;
    case ESP_RST_JTAG:
        return STARTUP_RESET_JTAG;
    case ESP_RST_EFUSE:
        return STARTUP_RESET_EFUSE;
    case ESP_RST_PWR_GLITCH:
        return STARTUP_RESET_POWER_GLITCH;
    case ESP_RST_CPU_LOCKUP:
        return STARTUP_RESET_CPU_LOCKUP;
    case ESP_RST_UNKNOWN:
    default:
        return STARTUP_RESET_UNKNOWN;
    }
}

static void mark_starting(startup_service_id_t service)
{
    startup_service_mark_starting(service, now_ms());
}

static void record_service(startup_service_id_t service,
                           startup_service_state_t state, esp_err_t err)
{
    (void)startup_service_record(
        service, state, (int32_t)err, now_ms());
}

static esp_err_t run_ready_service(startup_service_id_t service,
                                   esp_err_t (*init)(void))
{
    esp_err_t err;

    mark_starting(service);
    err = init();
    record_service(service,
                   err == ESP_OK ? STARTUP_SERVICE_READY
                                 : STARTUP_SERVICE_FAILED,
                   err);
    return err;
}

static void start_status_led_service(const app_config_t *config)
{
    esp_err_t err;

    if (!config->board.status_led_enabled) {
        record_service(STARTUP_SERVICE_STATUS_LED,
                       STARTUP_SERVICE_DISABLED, ESP_OK);
        return;
    }

    const status_led_config_t status_led_config = {
        .gpio = config->board.status_led_gpio,
        .active_high = config->board.status_led_active_high,
    };

    mark_starting(STARTUP_SERVICE_STATUS_LED);
    err = status_led_service_start(&status_led_config);
    record_service(STARTUP_SERVICE_STATUS_LED,
                   err == ESP_OK ? STARTUP_SERVICE_READY
                                 : STARTUP_SERVICE_FAILED,
                   err);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "board status LED unavailable: %s",
                 esp_err_to_name(err));
    }
}

static void start_indicator_service(const app_config_t *config)
{
    esp_err_t err;

    if (!config->board.rgb_led_enabled) {
        record_service(STARTUP_SERVICE_INDICATOR,
                       STARTUP_SERVICE_DISABLED, ESP_OK);
        return;
    }

    const rgb_indicator_config_t rgb_config = {
        .enabled = true,
        .red_gpio = config->board.rgb_red_gpio,
        .green_gpio = config->board.rgb_green_gpio,
        .blue_gpio = config->board.rgb_blue_gpio,
    };

    mark_starting(STARTUP_SERVICE_INDICATOR);
    err = rgb_indicator_init(&rgb_config);
    record_service(STARTUP_SERVICE_INDICATOR,
                   err == ESP_OK ? STARTUP_SERVICE_READY
                                 : STARTUP_SERVICE_FAILED,
                   err);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "RGB light-language service unavailable: %s",
                 esp_err_to_name(err));
    }
}

static void start_feedback_service(const app_config_t *config)
{
    const feedback_service_config_t feedback_config = {
        .buzzer_enabled = config->board.buzzer_enabled,
        .buzzer_gpio = config->board.buzzer_gpio,
        .vibration_enabled = config->board.vibration_enabled,
        .vibration_gpio = config->board.vibration_gpio,
    };
    feedback_service_snapshot_t feedback_snapshot = {0};
    esp_err_t err;

    if (!config->board.enabled) {
        record_service(STARTUP_SERVICE_FEEDBACK,
                       STARTUP_SERVICE_DISABLED, ESP_OK);
        return;
    }

    mark_starting(STARTUP_SERVICE_FEEDBACK);
    err = feedback_service_start(&feedback_config);
    (void)feedback_service_get_snapshot(&feedback_snapshot);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "feedback service partially unavailable: %s",
                 esp_err_to_name(err));
    }
    record_service(
        STARTUP_SERVICE_FEEDBACK,
        err == ESP_OK
            ? STARTUP_SERVICE_READY
            : feedback_snapshot.buzzer_ready ||
                      feedback_snapshot.vibration_ready
                  ? STARTUP_SERVICE_DEGRADED
                  : STARTUP_SERVICE_FAILED,
        err);
}

static void start_local_services(const app_config_t *config)
{
    const button_service_config_t button_config = {
        .gpio = {
            config->board.button_gpio[0],
            config->board.button_gpio[1],
            config->board.button_gpio[2],
            config->board.button_gpio[3],
        },
        .active_low = config->board.button_active_low,
        .debounce_ms = 25u,
        .long_press_ms = 800u,
    };
    esp_err_t err;

    if (!config->board.enabled) {
        record_service(STARTUP_SERVICE_STOCK,
                       STARTUP_SERVICE_DISABLED, ESP_OK);
        record_service(STARTUP_SERVICE_BUTTONS,
                       STARTUP_SERVICE_DISABLED, ESP_OK);
        record_service(STARTUP_SERVICE_MONITOR,
                       STARTUP_SERVICE_DISABLED, ESP_OK);
        return;
    }

    err = run_ready_service(STARTUP_SERVICE_STOCK, stock_service_start);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "stock service start failed: %s",
                 esp_err_to_name(err));
    }

    mark_starting(STARTUP_SERVICE_BUTTONS);
    err = button_service_start(&button_config);
    record_service(STARTUP_SERVICE_BUTTONS,
                   err == ESP_OK ? STARTUP_SERVICE_READY
                                 : STARTUP_SERVICE_FAILED,
                   err);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "button service start failed: %s",
                 esp_err_to_name(err));
    }

    err = run_ready_service(STARTUP_SERVICE_MONITOR, stock_monitor_start);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "stock monitor start failed: %s",
                 esp_err_to_name(err));
    }
}

static void start_wifi_service(const app_config_t *config)
{
    esp_err_t err;

    startup_service_set_stage(STARTUP_STAGE_NETWORK);
    mark_starting(STARTUP_SERVICE_WIFI);
    err = wifi_manager_init(&config->wifi);
    if (err == ESP_OK) {
        err = wifi_manager_start();
    }
    record_service(STARTUP_SERVICE_WIFI,
                   err == ESP_OK ? STARTUP_SERVICE_READY
                                 : STARTUP_SERVICE_FAILED,
                   err);
    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "Wi-Fi unavailable; continuing in local degraded mode: %s",
                 esp_err_to_name(err));
        return;
    }

    wifi_manager_status_t wifi_status;

    wifi_manager_get_status(&wifi_status);
    if (wifi_status.connected) {
        rgb_indicator_set_network_state(
            RGB_INDICATOR_NETWORK_CONNECTED);
    } else if (wifi_status.connecting) {
        rgb_indicator_set_network_state(
            RGB_INDICATOR_NETWORK_CONNECTING);
    } else if (!wifi_status.configured) {
        rgb_indicator_set_network_state(
            RGB_INDICATOR_NETWORK_LOCAL_AP);
    } else {
        rgb_indicator_set_network_state(
            RGB_INDICATOR_NETWORK_DISCONNECTED);
    }
}

static void start_http_service(const app_config_t *config)
{
    esp_err_t err = ESP_OK;

    if (!config->http_server_enabled) {
        record_service(STARTUP_SERVICE_HTTP,
                       STARTUP_SERVICE_DISABLED, ESP_OK);
        return;
    }

    startup_service_set_stage(STARTUP_STAGE_HTTP);
    mark_starting(STARTUP_SERVICE_HTTP);
    for (size_t attempt = 1u; attempt <= HTTP_API_START_ATTEMPTS; attempt++) {
        err = http_api_start();
        if (err == ESP_OK) {
            break;
        }
        ESP_LOGW(TAG, "HTTP start attempt %u/%u failed: %s",
                 (unsigned)attempt, (unsigned)HTTP_API_START_ATTEMPTS,
                 esp_err_to_name(err));
        if (attempt < HTTP_API_START_ATTEMPTS) {
            vTaskDelay(pdMS_TO_TICKS(HTTP_API_START_RETRY_DELAY_MS));
        }
    }
    record_service(STARTUP_SERVICE_HTTP,
                   err == ESP_OK ? STARTUP_SERVICE_READY
                                 : STARTUP_SERVICE_FAILED,
                   err);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP API unavailable: %s", esp_err_to_name(err));
    }
}

static void log_chip_info(void)
{
    esp_chip_info_t chip = {0};
    esp_chip_info(&chip);

    ESP_LOGI(TAG, "chip: ESP32, cores: %d, revision: v%d.%d",
             chip.cores, chip.revision / 100, chip.revision % 100);
    ESP_LOGI(TAG, "framework version: %s", app_config_get()->version);
}

static void log_startup_summary(void)
{
    startup_snapshot_t snapshot;

    startup_service_get_snapshot(&snapshot);
    ESP_LOGI(TAG, "startup %s in %lu ms (reset=%s)",
             startup_state_name(snapshot.state),
             (unsigned long)snapshot.duration_ms,
             startup_reset_reason_name(snapshot.reset_reason));

    for (size_t index = 0u; index < STARTUP_SERVICE_COUNT; index++) {
        const startup_service_record_t *record = &snapshot.services[index];

        if (record->state == STARTUP_SERVICE_FAILED ||
            record->state == STARTUP_SERVICE_DEGRADED) {
            ESP_LOGW(TAG, "service %s: %s (%s, %lu ms)",
                     startup_service_name((startup_service_id_t)index),
                     startup_service_state_name(record->state),
                     esp_err_to_name((esp_err_t)record->last_error),
                     (unsigned long)record->duration_ms);
        }
    }
}

void app_main(void)
{
    const app_config_t *config;
    const startup_reset_reason_t reset_reason =
        reset_reason_from_esp(esp_reset_reason());
    esp_err_t err;

    startup_service_begin(now_ms(), reset_reason);
    config = app_config_get();
    ESP_LOGI(TAG, "boot reset reason: %s",
             startup_reset_reason_name(reset_reason));
    ESP_LOGI(TAG, "starting %s", config->device_name);
    log_chip_info();

    startup_service_set_stage(STARTUP_STAGE_CORE);
    start_status_led_service(config);

    err = run_ready_service(STARTUP_SERVICE_CONFIG, config_service_init);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "configuration storage unavailable; entering safe mode: %s",
                 esp_err_to_name(err));
    }
    err = run_ready_service(STARTUP_SERVICE_EVENTS, app_events_init);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "event dispatcher unavailable; entering safe mode: %s",
                 esp_err_to_name(err));
    }

    start_indicator_service(config);
    start_feedback_service(config);

    startup_service_set_stage(STARTUP_STAGE_LOCAL);
    start_local_services(config);

    start_wifi_service(config);
    start_http_service(config);

    startup_service_set_stage(STARTUP_STAGE_BACKGROUND);
    err = run_ready_service(STARTUP_SERVICE_TIME, time_service_init);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "time service unavailable: %s", esp_err_to_name(err));
    }

    startup_service_finish(now_ms());
    {
        startup_snapshot_t final_snapshot;

        startup_service_get_snapshot(&final_snapshot);
        rgb_indicator_set_system_state(
            final_snapshot.state == STARTUP_STATE_READY
                ? RGB_INDICATOR_SYSTEM_READY
                : RGB_INDICATOR_SYSTEM_DEGRADED);
        if (final_snapshot.state == STARTUP_STATE_READY) {
            status_led_service_set_mode(STATUS_LED_MODE_READY);
        } else if (final_snapshot.state == STARTUP_STATE_SAFE_MODE ||
                   final_snapshot.state == STARTUP_STATE_FAILED) {
            status_led_service_set_mode(STATUS_LED_MODE_ERROR);
        } else {
            status_led_service_set_mode(STATUS_LED_MODE_DEGRADED);
        }
        if (final_snapshot.state != STARTUP_STATE_READY) {
            (void)app_events_publish(
                APP_EVENT_SYSTEM_DEGRADED, APP_EVENT_SOURCE_SYSTEM,
                APP_EVENT_SEVERITY_WARNING, final_snapshot.state, 0, 0);
        }
    }
    log_startup_summary();
}
