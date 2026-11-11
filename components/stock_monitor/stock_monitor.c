#include "stock_monitor.h"

#include <stddef.h>
#include <string.h>

#include "app_config.h"
#include "app_events.h"
#include "button_service.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "feedback_service.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "oled_display.h"
#include "rgb_indicator.h"
#include "stock_monitor_alert.h"
#include "stock_monitor_state.h"
#include "stock_monitor_ui.h"
#include "stock_service.h"

#define STOCK_MONITOR_TASK_STACK 8192u
#define STOCK_MONITOR_TASK_PRIORITY 4u
#define STOCK_MONITOR_POLL_MS 25u
#define STOCK_MONITOR_RENDER_MS 250u
#define STOCK_MONITOR_OLED_RETRY_MS 2000u

static const char *TAG = "stock_monitor";

static stock_monitor_state_t s_state;
static stock_service_snapshot_t s_snapshot;
static stock_service_config_t s_stock_config;
static bool s_started;
static TaskHandle_t s_task;
static uint32_t s_last_refresh_count;
static uint32_t s_last_failure_count;
static bool s_manual_refresh_pending;
static stock_monitor_alert_arm_t s_alert_armed[STOCK_SERVICE_MAX_SYMBOLS];
static bool s_oled_failure_logged;
static int64_t s_last_oled_attempt_us;
static uint64_t s_last_config_poll_us;

static uint64_t now_us(void)
{
    return (uint64_t)esp_timer_get_time();
}

static void reset_price_alerts(void)
{
    for (size_t index = 0u; index < STOCK_SERVICE_MAX_SYMBOLS; index++) {
        stock_monitor_alert_arm_reset(&s_alert_armed[index]);
    }
}

static feedback_mode_t feedback_mode_from_stock(stock_feedback_mode_t mode)
{
    switch (mode) {
    case STOCK_FEEDBACK_VIBRATE:
        return FEEDBACK_MODE_VIBRATE;
    case STOCK_FEEDBACK_SOUND:
        return FEEDBACK_MODE_SOUND;
    case STOCK_FEEDBACK_SILENT:
        return FEEDBACK_MODE_SILENT;
    case STOCK_FEEDBACK_BOTH:
    default:
        return FEEDBACK_MODE_BOTH;
    }
}

static rgb_indicator_stock_state_t rgb_stock_state(
    stock_monitor_data_state_t state)
{
    switch (state) {
    case STOCK_MONITOR_DATA_EMPTY:
        return RGB_INDICATOR_STOCK_EMPTY;
    case STOCK_MONITOR_DATA_WAITING:
        return RGB_INDICATOR_STOCK_WAITING;
    case STOCK_MONITOR_DATA_FRESH:
        return RGB_INDICATOR_STOCK_FRESH;
    case STOCK_MONITOR_DATA_STALE:
        return RGB_INDICATOR_STOCK_STALE;
    case STOCK_MONITOR_DATA_ERROR:
        return RGB_INDICATOR_STOCK_ERROR;
    default:
        return RGB_INDICATOR_STOCK_UNKNOWN;
    }
}

static void refresh_monitor_config(void)
{
    const uint64_t now = now_us();
    stock_service_config_t latest;

    if (now - s_last_config_poll_us < 1000000u) {
        return;
    }
    s_last_config_poll_us = now;
    stock_service_get_config(&latest);
    if (latest.feedback_mode != s_stock_config.feedback_mode) {
        esp_err_t err = feedback_service_set_mode(
            feedback_mode_from_stock(latest.feedback_mode));

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "feedback mode update failed: %s",
                     esp_err_to_name(err));
        }
    }
    if (latest.alerts_enabled != s_stock_config.alerts_enabled) {
        reset_price_alerts();
    } else {
        for (size_t index = 0u; index < STOCK_SERVICE_MAX_SYMBOLS;
             index++) {
            if (strcmp(latest.symbols[index],
                       s_stock_config.symbols[index]) != 0 ||
                latest.alert_upper_price_units[index] !=
                    s_stock_config.alert_upper_price_units[index] ||
                latest.alert_lower_price_units[index] !=
                    s_stock_config.alert_lower_price_units[index]) {
                stock_monitor_alert_arm_reset(&s_alert_armed[index]);
            }
        }
    }
    s_stock_config = latest;
}

static void handle_button(const button_event_t *event)
{
    stock_monitor_event_t state_event = STOCK_MONITOR_EVENT_NONE;
    stock_monitor_output_t output;

    if (event->button == BUTTON_SERVICE_K1) {
        state_event = event->type == BUTTON_EVENT_LONG_PRESS
                          ? STOCK_MONITOR_EVENT_FIRST
                          : STOCK_MONITOR_EVENT_PREVIOUS;
    } else if (event->button == BUTTON_SERVICE_K2) {
        state_event = event->type == BUTTON_EVENT_LONG_PRESS
                          ? STOCK_MONITOR_EVENT_LAST
                          : STOCK_MONITOR_EVENT_NEXT;
    } else if (event->button == BUTTON_SERVICE_K3) {
        state_event = STOCK_MONITOR_EVENT_REFRESH_REQUESTED;
    } else if (event->button == BUTTON_SERVICE_K4) {
        state_event = event->type == BUTTON_EVENT_LONG_PRESS
                          ? STOCK_MONITOR_EVENT_SHOW_QUOTE
                          : STOCK_MONITOR_EVENT_CYCLE_VIEW;
    }

    output = stock_monitor_state_apply(&s_state, state_event);
    if (output == STOCK_MONITOR_OUTPUT_NAVIGATION) {
        (void)app_events_publish(
            APP_EVENT_STOCK_VIEW_CHANGED, APP_EVENT_SOURCE_MONITOR,
            APP_EVENT_SEVERITY_INFO, (int32_t)s_state.selected_index,
            (int32_t)s_state.view, 0);
    } else if (output == STOCK_MONITOR_OUTPUT_REFRESH_REQUESTED) {
        esp_err_t err = stock_service_refresh_now();

        if (err == ESP_OK) {
            s_manual_refresh_pending = true;
            (void)app_events_publish(
                APP_EVENT_STOCK_REFRESH_STARTED, APP_EVENT_SOURCE_MONITOR,
                APP_EVENT_SEVERITY_INFO, (int32_t)s_state.selected_index,
                0, 0);
        }
    }
}

static void update_data_state(const stock_service_snapshot_t *snapshot)
{
    stock_monitor_event_t event;

    if (snapshot->refreshing && !snapshot->any_valid) {
        event = STOCK_MONITOR_EVENT_DATA_WAITING;
    } else if (snapshot->symbol_count == 0u) {
        event = STOCK_MONITOR_EVENT_DATA_EMPTY;
    } else if (snapshot->any_valid) {
        uint32_t stale_after =
            (uint32_t)snapshot->refresh_seconds * 3u;

        if (stale_after < 300u) {
            stale_after = 300u;
        }
        event = snapshot->seconds_since_update > stale_after
                    ? STOCK_MONITOR_EVENT_DATA_STALE
                    : STOCK_MONITOR_EVENT_DATA_FRESH;
    } else if (snapshot->last_error[0] != '\0') {
        event = STOCK_MONITOR_EVENT_DATA_ERROR;
    } else {
        event = STOCK_MONITOR_EVENT_DATA_WAITING;
    }
    (void)stock_monitor_state_apply(&s_state, event);
}

static void maybe_alert_price(const stock_service_snapshot_t *snapshot)
{
    if (!s_stock_config.alerts_enabled) {
        return;
    }
    for (size_t index = 0u; index < snapshot->symbol_count; index++) {
        const stock_quote_t *quote = &snapshot->quotes[index];
        stock_monitor_alert_direction_t direction;

        if (!quote->valid) {
            stock_monitor_alert_arm_reset(&s_alert_armed[index]);
            continue;
        }
        direction = stock_monitor_alert_evaluate(
            quote->price,
            s_stock_config.alert_upper_price_units[index],
            s_stock_config.alert_lower_price_units[index],
            &s_alert_armed[index]);
        if (direction == STOCK_MONITOR_ALERT_NONE) {
            continue;
        }
        (void)app_events_publish(
            APP_EVENT_STOCK_PRICE_ALERT, APP_EVENT_SOURCE_STOCK,
            APP_EVENT_SEVERITY_WARNING, (int32_t)index,
            direction == STOCK_MONITOR_ALERT_UP ? 1 : -1, 0);
        ESP_LOGW(TAG, "%s %s price limit reached: %.2f",
                 quote->symbol,
                 direction == STOCK_MONITOR_ALERT_UP ? "up" : "down",
                 quote->price);
    }
}

static void handle_refresh_result(const stock_service_snapshot_t *snapshot)
{
    bool had_previous_refresh = s_last_refresh_count != 0u;
    bool refreshed = snapshot->refresh_count != s_last_refresh_count;
    bool failed = snapshot->failure_count != s_last_failure_count;

    if (failed) {
        s_last_failure_count = snapshot->failure_count;
        if (s_manual_refresh_pending) {
            (void)app_events_publish(
                APP_EVENT_STOCK_REFRESH_FAILED, APP_EVENT_SOURCE_STOCK,
                APP_EVENT_SEVERITY_WARNING,
                (int32_t)snapshot->failure_count, 0, 0);
        }
    }
    if (refreshed) {
        s_last_refresh_count = snapshot->refresh_count;
        if (s_manual_refresh_pending) {
            (void)app_events_publish(
                APP_EVENT_STOCK_REFRESHED, APP_EVENT_SOURCE_STOCK,
                APP_EVENT_SEVERITY_INFO,
                (int32_t)snapshot->refresh_count, 0, 0);
        }
        if (had_previous_refresh) {
            maybe_alert_price(snapshot);
        }
    }
    if (s_manual_refresh_pending && !snapshot->refreshing &&
        !refreshed && !failed) {
        return;
    }
    if (!snapshot->refreshing) {
        s_manual_refresh_pending = false;
    }
}

static bool ensure_oled_ready(int64_t current_us)
{
    const app_config_t *app = app_config_get();
    const app_board_config_t *board = &app->board;
    oled_display_config_t config;
    esp_err_t err;

    if (oled_display_is_ready()) {
        return true;
    }
    if (current_us - s_last_oled_attempt_us <
        (int64_t)STOCK_MONITOR_OLED_RETRY_MS * 1000LL) {
        return false;
    }
    s_last_oled_attempt_us = current_us;
    config = (oled_display_config_t){
        .i2c_port = board->oled_i2c_port,
        .sda_gpio = board->oled_sda_gpio,
        .scl_gpio = board->oled_scl_gpio,
        .clock_hz = board->oled_i2c_hz,
        .controller =
            board->oled_controller == APP_OLED_CONTROLLER_SH1106
                ? OLED_DISPLAY_CONTROLLER_SH1106
                : OLED_DISPLAY_CONTROLLER_SSD1306,
    };
    err = oled_display_init(&config);
    if (err != ESP_OK) {
        if (!s_oled_failure_logged) {
            ESP_LOGW(TAG, "OLED not ready at 0x3C/0x3D: %s",
                     esp_err_to_name(err));
            s_oled_failure_logged = true;
        }
        return false;
    }
    ESP_LOGI(TAG, "OLED ready at 0x%02X, controller=%s",
             oled_display_address(),
             oled_display_controller() == OLED_DISPLAY_CONTROLLER_SH1106
                 ? "SH1106"
                 : "SSD1306");
    s_oled_failure_logged = false;
    return true;
}

static void stock_monitor_task(void *argument)
{
    TickType_t last_wake = xTaskGetTickCount();
    int64_t last_render_us = 0;

    (void)argument;
    ESP_LOGI(TAG, "monitor task running");

    while (true) {
        button_event_t button_event;
        int64_t current_us = (int64_t)now_us();

        memset(&s_snapshot, 0, sizeof(s_snapshot));
        stock_service_get_snapshot(&s_snapshot);
        stock_monitor_state_set_symbol_count(&s_state,
                                             s_snapshot.symbol_count);
        refresh_monitor_config();
        update_data_state(&s_snapshot);
        rgb_indicator_set_stock_state(
            rgb_stock_state(s_state.data_state), s_snapshot.refreshing,
            s_snapshot.symbol_count > 0u);
        handle_refresh_result(&s_snapshot);

        while (button_service_read_event(&button_event, 0u)) {
            handle_button(&button_event);
        }

        if (ensure_oled_ready(current_us) &&
            current_us - last_render_us >=
                (int64_t)STOCK_MONITOR_RENDER_MS * 1000LL) {
            stock_monitor_ui_render(&s_snapshot, &s_state);
            if (oled_display_flush() != ESP_OK) {
                ESP_LOGW(TAG, "OLED write failed; will retry");
                s_oled_failure_logged = true;
            }
            last_render_us = current_us;
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(STOCK_MONITOR_POLL_MS));
    }
}

esp_err_t stock_monitor_start(void)
{
    const app_config_t *app = app_config_get();
    esp_err_t err;

    if (s_started) {
        return ESP_OK;
    }
    if (!app->board.enabled || !stock_service_is_started()) {
        return ESP_ERR_INVALID_STATE;
    }

    stock_service_get_config(&s_stock_config);
    stock_monitor_state_init(&s_state, s_stock_config.symbol_count);
    reset_price_alerts();
    s_last_refresh_count = 0u;
    s_last_failure_count = 0u;
    s_manual_refresh_pending = false;
    s_last_oled_attempt_us =
        -(int64_t)STOCK_MONITOR_OLED_RETRY_MS * 1000LL;
    s_last_config_poll_us = 0u;
    memset(&s_snapshot, 0, sizeof(s_snapshot));

    /*
     * Bring the display up before the monitor task starts so wiring and
     * controller failures are reported during startup and the panel shows
     * a waiting screen even while network data is unavailable.
     */
    if (ensure_oled_ready((int64_t)now_us())) {
        stock_service_get_snapshot(&s_snapshot);
        update_data_state(&s_snapshot);
        stock_monitor_ui_render(&s_snapshot, &s_state);
        if (oled_display_flush() != ESP_OK) {
            ESP_LOGW(TAG, "initial OLED write failed; monitor will retry");
            s_oled_failure_logged = true;
        }
    } else {
        ESP_LOGW(TAG, "initial OLED bring-up failed; monitor will retry");
    }

    err = feedback_service_set_mode(
        feedback_mode_from_stock(s_stock_config.feedback_mode));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "feedback mode unavailable: %s",
                 esp_err_to_name(err));
    }

    if (xTaskCreate(stock_monitor_task, "stock_monitor",
                    STOCK_MONITOR_TASK_STACK, NULL,
                    STOCK_MONITOR_TASK_PRIORITY, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    ESP_LOGI(TAG, "started; K1/K2 browse, K3 sync, K4 view");
    return ESP_OK;
}
