#include "app_config.h"
#include "app_events.h"
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
#include "startup_service.h"
#include "time_service.h"
#include "weather_service.h"
#include "wellness_monitor.h"
#include "wifi_manager.h"

static const char *TAG = "app";

#define HTTP_API_START_ATTEMPTS 3u
#define HTTP_API_START_RETRY_DELAY_MS 250u
#define FATAL_STARTUP_LOG_DELAY_MS 3000u

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

static void restart_after_fatal_error(startup_service_id_t service,
                                      esp_err_t err)
{
    startup_snapshot_t snapshot;

    startup_service_finish(now_ms());
    startup_service_get_snapshot(&snapshot);
    ESP_LOGE(TAG, "boot-critical service %s failed: %s",
             startup_service_name(service), esp_err_to_name(err));
    ESP_LOGE(TAG, "startup blocked after %lu ms; restarting",
             (unsigned long)snapshot.duration_ms);

    /* Leave enough time for the failure trail to reach the serial console. */
    vTaskDelay(pdMS_TO_TICKS(FATAL_STARTUP_LOG_DELAY_MS));
    esp_restart();
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
    ESP_LOGI(TAG, "boot reset reason: %s",
             startup_reset_reason_name(reset_reason));

    startup_service_set_stage(STARTUP_STAGE_CORE);
    err = run_ready_service(STARTUP_SERVICE_CONFIG, config_service_init);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "configuration storage unavailable; continuing in degraded "
                 "mode: %s",
                 esp_err_to_name(err));
    }
    err = run_ready_service(STARTUP_SERVICE_EVENTS, app_events_init);
    if (err != ESP_OK) {
        restart_after_fatal_error(STARTUP_SERVICE_EVENTS, err);
    }
    config = app_config_get();

    ESP_LOGI(TAG, "starting %s", config->device_name);
    log_chip_info();

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
        ESP_LOGW(TAG, "Wi-Fi unavailable; continuing in local degraded mode: "
                      "%s",
                 esp_err_to_name(err));
    }

    startup_service_set_stage(STARTUP_STAGE_TIME);
    err = run_ready_service(STARTUP_SERVICE_TIME, time_service_init);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "time service unavailable: %s", esp_err_to_name(err));
    }

    startup_service_set_stage(STARTUP_STAGE_SERVICES);
    err = run_ready_service(STARTUP_SERVICE_WEATHER, weather_service_init);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "weather service unavailable: %s",
                 esp_err_to_name(err));
    }

    if (config->wellness.enabled) {
        const feedback_service_config_t feedback_config = {
            .buzzer_enabled = config->wellness.buzzer_enabled,
            .buzzer_gpio = config->wellness.buzzer_gpio,
            .vibration_enabled = config->wellness.vibration_enabled,
            .vibration_gpio = config->wellness.vibration_gpio,
        };

        feedback_service_snapshot_t feedback_snapshot = {0};
        esp_err_t feedback_err;

        mark_starting(STARTUP_SERVICE_FEEDBACK);
        feedback_err = feedback_service_start(&feedback_config);
        (void)feedback_service_get_snapshot(&feedback_snapshot);
        if (feedback_err != ESP_OK) {
            ESP_LOGW(TAG, "feedback service partially unavailable: %s",
                     esp_err_to_name(feedback_err));
        }
        record_service(
            STARTUP_SERVICE_FEEDBACK,
            feedback_err == ESP_OK
                ? STARTUP_SERVICE_READY
                : feedback_snapshot.buzzer_ready ||
                          feedback_snapshot.vibration_ready
                      ? STARTUP_SERVICE_DEGRADED
                      : STARTUP_SERVICE_FAILED,
            feedback_err);

        err = run_ready_service(
            STARTUP_SERVICE_WELLNESS, wellness_monitor_start);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "wellness monitor start failed: %s",
                     esp_err_to_name(err));
        }
    } else {
        record_service(STARTUP_SERVICE_FEEDBACK,
                       STARTUP_SERVICE_DISABLED, ESP_OK);
        record_service(STARTUP_SERVICE_WELLNESS,
                       STARTUP_SERVICE_DISABLED, ESP_OK);
    }

    if (config->http_server_enabled) {
        startup_service_set_stage(STARTUP_STAGE_HTTP);
        mark_starting(STARTUP_SERVICE_HTTP);
        for (size_t attempt = 1u; attempt <= HTTP_API_START_ATTEMPTS;
             attempt++) {
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
    } else {
        record_service(STARTUP_SERVICE_HTTP,
                       STARTUP_SERVICE_DISABLED, ESP_OK);
    }

    startup_service_finish(now_ms());
    log_startup_summary();
}
