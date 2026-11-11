#include "rgb_indicator.h"

#include <stddef.h>
#include <string.h>

#include "app_events.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define RGB_INDICATOR_TASK_STACK 3072u
#define RGB_INDICATOR_TASK_PRIORITY 3u
#define RGB_INDICATOR_TICK_MS 20u
#define RGB_INDICATOR_LEDC_MODE LEDC_LOW_SPEED_MODE
#define RGB_INDICATOR_LEDC_TIMER LEDC_TIMER_0
#define RGB_INDICATOR_LEDC_RESOLUTION LEDC_TIMER_10_BIT
#define RGB_INDICATOR_LEDC_FREQUENCY_HZ 5000u
#define RGB_INDICATOR_LEDC_MAX_DUTY 1023u

static const char *TAG = "rgb_indicator";
static rgb_indicator_config_t s_config;
static rgb_indicator_context_t s_context;
static rgb_indicator_event_state_t s_events;
static rgb_indicator_resolved_t s_resolved;
static portMUX_TYPE s_state_mux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_task;
static bool s_initialized;
static bool s_started;
static int s_channel_duty[3] = {-1, -1, -1};

static const ledc_channel_t s_channels[] = {
    LEDC_CHANNEL_0,
    LEDC_CHANNEL_1,
    LEDC_CHANNEL_2,
};

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool is_valid_gpio(int gpio)
{
    return gpio >= 0 && GPIO_IS_VALID_OUTPUT_GPIO(gpio);
}

static esp_err_t write_channel(size_t index, uint8_t value,
                               uint8_t brightness, uint8_t pulse)
{
    uint32_t scaled =
        (uint32_t)value * brightness * pulse / 10000u;
    uint32_t gamma;
    uint32_t duty;

    if (scaled > 255u) {
        scaled = 255u;
    }
    gamma = (scaled * scaled + 127u) / 255u;
    duty = gamma * RGB_INDICATOR_LEDC_MAX_DUTY / 255u;

    if (s_channel_duty[index] == (int)duty) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(
        ledc_set_duty(RGB_INDICATOR_LEDC_MODE, s_channels[index], duty), TAG,
        "failed to set RGB channel %u duty", (unsigned)index);
    ESP_RETURN_ON_ERROR(
        ledc_update_duty(RGB_INDICATOR_LEDC_MODE, s_channels[index]), TAG,
        "failed to apply RGB channel %u duty", (unsigned)index);
    s_channel_duty[index] = (int)duty;
    return ESP_OK;
}

static esp_err_t write_rgb(const rgb_indicator_resolved_t *resolved)
{
    ESP_RETURN_ON_ERROR(
        write_channel(0, resolved->red, resolved->brightness,
                      resolved->pulse_percent),
        TAG, "failed to update red output");
    ESP_RETURN_ON_ERROR(
        write_channel(1, resolved->green, resolved->brightness,
                      resolved->pulse_percent),
        TAG, "failed to update green output");
    return write_channel(2, resolved->blue, resolved->brightness,
                         resolved->pulse_percent);
}

static void on_app_event(const app_event_t *event, void *context)
{
    rgb_indicator_event_t indicator_event = RGB_INDICATOR_EVENT_NONE;

    (void)context;
    if (event == NULL || !s_started) {
        return;
    }

    switch (event->type) {
    case APP_EVENT_WIFI_STATION_CONNECTING:
        rgb_indicator_set_network_state(RGB_INDICATOR_NETWORK_CONNECTING);
        break;
    case APP_EVENT_WIFI_STATION_CONNECTED:
        rgb_indicator_set_network_state(RGB_INDICATOR_NETWORK_CONNECTED);
        indicator_event = RGB_INDICATOR_EVENT_WIFI_CONNECTED;
        break;
    case APP_EVENT_WIFI_STATION_DISCONNECTED:
        rgb_indicator_set_network_state(RGB_INDICATOR_NETWORK_DISCONNECTED);
        indicator_event = RGB_INDICATOR_EVENT_WIFI_DISCONNECTED;
        break;
    case APP_EVENT_WIFI_CLIENT_CONNECTED:
        indicator_event = RGB_INDICATOR_EVENT_CLIENT_CONNECTED;
        break;
    case APP_EVENT_WIFI_CLIENT_DISCONNECTED:
        indicator_event = RGB_INDICATOR_EVENT_CLIENT_DISCONNECTED;
        break;
    case APP_EVENT_STOCK_REFRESH_STARTED:
        indicator_event = RGB_INDICATOR_EVENT_REFRESH_STARTED;
        break;
    case APP_EVENT_STOCK_REFRESHED:
        indicator_event = RGB_INDICATOR_EVENT_REFRESH_SUCCEEDED;
        break;
    case APP_EVENT_STOCK_REFRESH_FAILED:
        indicator_event = RGB_INDICATOR_EVENT_REFRESH_FAILED;
        break;
    case APP_EVENT_STOCK_PRICE_ALERT:
        if (event->detail < 0) {
            indicator_event = RGB_INDICATOR_EVENT_PRICE_ALERT_DOWN;
        } else if (event->detail > 0) {
            indicator_event = RGB_INDICATOR_EVENT_PRICE_ALERT_UP;
        }
        break;
    case APP_EVENT_CONFIG_UPDATED:
        indicator_event = RGB_INDICATOR_EVENT_CONFIG_SAVED;
        break;
    case APP_EVENT_CONFIG_SAVE_FAILED:
        indicator_event = RGB_INDICATOR_EVENT_CONFIG_SAVE_FAILED;
        break;
    case APP_EVENT_STOCK_VIEW_CHANGED:
        indicator_event = RGB_INDICATOR_EVENT_NAVIGATE;
        break;
    case APP_EVENT_SYSTEM_DEGRADED:
        indicator_event = RGB_INDICATOR_EVENT_SYSTEM_DEGRADED;
        break;
    default:
        break;
    }

    if (indicator_event != RGB_INDICATOR_EVENT_NONE) {
        (void)rgb_indicator_notify(indicator_event);
    }
}

static void render_state(void)
{
    rgb_indicator_resolved_t resolved;
    esp_err_t err;

    taskENTER_CRITICAL(&s_state_mux);
    resolved = rgb_indicator_resolve(&s_context, &s_events, now_ms());
    s_resolved = resolved;
    taskEXIT_CRITICAL(&s_state_mux);

    err = write_rgb(&resolved);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "RGB output update failed: %s", esp_err_to_name(err));
    }
}

static void rgb_indicator_task(void *argument)
{
    TickType_t last_wake = xTaskGetTickCount();

    (void)argument;

    while (true) {
        render_state();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(RGB_INDICATOR_TICK_MS));
    }
}

static void stop_outputs(void)
{
    for (size_t index = 0;
         index < sizeof(s_channels) / sizeof(s_channels[0]); index++) {
        (void)ledc_stop(RGB_INDICATOR_LEDC_MODE, s_channels[index], 0);
        s_channel_duty[index] = -1;
    }
}

esp_err_t rgb_indicator_init(const rgb_indicator_config_t *config)
{
    ledc_timer_config_t timer_config = {
        .speed_mode = RGB_INDICATOR_LEDC_MODE,
        .duty_resolution = RGB_INDICATOR_LEDC_RESOLUTION,
        .timer_num = RGB_INDICATOR_LEDC_TIMER,
        .freq_hz = RGB_INDICATOR_LEDC_FREQUENCY_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err;

    if (s_initialized) {
        return s_started ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    if (config == NULL || !config->enabled) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!is_valid_gpio(config->red_gpio) ||
        !is_valid_gpio(config->green_gpio) ||
        !is_valid_gpio(config->blue_gpio) ||
        config->red_gpio == config->green_gpio ||
        config->red_gpio == config->blue_gpio ||
        config->green_gpio == config->blue_gpio) {
        return ESP_ERR_INVALID_ARG;
    }

    s_config = *config;
    s_context = (rgb_indicator_context_t){
        .system = RGB_INDICATOR_SYSTEM_BOOTING,
        .network = RGB_INDICATOR_NETWORK_UNKNOWN,
        .stock = RGB_INDICATOR_STOCK_UNKNOWN,
    };
    memset(&s_events, 0, sizeof(s_events));
    memset(&s_resolved, 0, sizeof(s_resolved));
    for (size_t index = 0;
         index < sizeof(s_channels) / sizeof(s_channels[0]); index++) {
        s_channel_duty[index] = -1;
    }

    err = ledc_timer_config(&timer_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC timer init failed: %s", esp_err_to_name(err));
        return err;
    }

    const int configured_gpios[] = {
        s_config.red_gpio,
        s_config.green_gpio,
        s_config.blue_gpio,
    };
    for (size_t index = 0;
         index < sizeof(s_channels) / sizeof(s_channels[0]); index++) {
        ledc_channel_config_t channel_config = {
            .gpio_num = configured_gpios[index],
            .speed_mode = RGB_INDICATOR_LEDC_MODE,
            .channel = s_channels[index],
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = RGB_INDICATOR_LEDC_TIMER,
            .duty = 0,
            .hpoint = 0,
            .flags = {
                .output_invert = 0,
            },
        };

        err = ledc_channel_config(&channel_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "LEDC channel %u init failed on GPIO%d: %s",
                     (unsigned)index, configured_gpios[index],
                     esp_err_to_name(err));
            stop_outputs();
            return err;
        }
    }

    err = app_events_subscribe(on_app_event, NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "event subscription unavailable; status polling remains "
                 "active: %s",
                 esp_err_to_name(err));
    }

    s_initialized = true;
    if (xTaskCreate(rgb_indicator_task, "rgb_indicator",
                    RGB_INDICATOR_TASK_STACK, NULL,
                    RGB_INDICATOR_TASK_PRIORITY, &s_task) != pdPASS) {
        stop_outputs();
        s_initialized = false;
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    ESP_LOGI(TAG, "light-language service ready on R=%d G=%d B=%d",
             s_config.red_gpio, s_config.green_gpio, s_config.blue_gpio);
    return ESP_OK;
}

void rgb_indicator_set_system_state(rgb_indicator_system_state_t state)
{
    if (!s_started || state < RGB_INDICATOR_SYSTEM_BOOTING ||
        state > RGB_INDICATOR_SYSTEM_DEGRADED) {
        return;
    }

    taskENTER_CRITICAL(&s_state_mux);
    s_context.system = state;
    taskEXIT_CRITICAL(&s_state_mux);
}

void rgb_indicator_set_network_state(rgb_indicator_network_state_t state)
{
    if (!s_started || state < RGB_INDICATOR_NETWORK_UNKNOWN ||
        state > RGB_INDICATOR_NETWORK_CONNECTED) {
        return;
    }

    taskENTER_CRITICAL(&s_state_mux);
    s_context.network = state;
    taskEXIT_CRITICAL(&s_state_mux);
}

void rgb_indicator_set_stock_state(rgb_indicator_stock_state_t state,
                                   bool refreshing,
                                   bool symbols_configured)
{
    if (!s_started || state < RGB_INDICATOR_STOCK_UNKNOWN ||
        state > RGB_INDICATOR_STOCK_ERROR) {
        return;
    }

    taskENTER_CRITICAL(&s_state_mux);
    s_context.stock = state;
    s_context.refreshing = refreshing;
    s_context.symbols_configured = symbols_configured;
    taskEXIT_CRITICAL(&s_state_mux);
}

bool rgb_indicator_notify(rgb_indicator_event_t event)
{
    bool accepted = false;

    if (!s_started) {
        return false;
    }

    taskENTER_CRITICAL(&s_state_mux);
    accepted = rgb_indicator_trigger(&s_events, event, now_ms());
    taskEXIT_CRITICAL(&s_state_mux);
    return accepted;
}

bool rgb_indicator_get_snapshot(rgb_indicator_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }

    taskENTER_CRITICAL(&s_state_mux);
    *snapshot = (rgb_indicator_snapshot_t){
        .enabled = s_initialized,
        .ready = s_started,
        .context = s_context,
        .base_scene = s_resolved.base_scene,
        .scene = s_resolved.scene,
        .active_event = s_resolved.active_event,
        .event_sequence = s_events.sequence,
        .red = s_resolved.red,
        .green = s_resolved.green,
        .blue = s_resolved.blue,
        .brightness = s_resolved.brightness,
        .pulse_percent = s_resolved.pulse_percent,
    };
    taskEXIT_CRITICAL(&s_state_mux);
    return s_started;
}
