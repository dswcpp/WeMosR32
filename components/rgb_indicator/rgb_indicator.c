#include "rgb_indicator.h"

#include <stddef.h>
#include <string.h>

#include "config_service.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define RGB_INDICATOR_TASK_STACK 3072u
#define RGB_INDICATOR_TASK_PRIORITY 3u
#define RGB_INDICATOR_TICK_MS 40u
#define RGB_INDICATOR_LEDC_MODE LEDC_LOW_SPEED_MODE
#define RGB_INDICATOR_LEDC_TIMER LEDC_TIMER_0
#define RGB_INDICATOR_LEDC_RESOLUTION LEDC_TIMER_10_BIT
#define RGB_INDICATOR_LEDC_FREQUENCY_HZ 5000u
#define RGB_INDICATOR_LEDC_MAX_DUTY 1023u
#define RGB_CONFIG_MAGIC 0x52474231u
#define RGB_CONFIG_VERSION 1u

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t mode;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t brightness;
    uint8_t reserved;
} rgb_persisted_config_t;

static const char *TAG = "rgb_indicator";
static rgb_indicator_config_t s_config;
static rgb_indicator_snapshot_t s_state;
static portMUX_TYPE s_state_mux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_task;
static bool s_initialized;
static bool s_started;
static int s_channel_duty[3] = {-1, -1, -1};
static uint8_t s_rainbow_hue;

static const ledc_channel_t s_channels[] = {
    LEDC_CHANNEL_0,
    LEDC_CHANNEL_1,
    LEDC_CHANNEL_2,
};

static bool is_valid_mode(rgb_indicator_mode_t mode)
{
    return mode >= RGB_INDICATOR_MODE_OFF &&
           mode <= RGB_INDICATOR_MODE_RAINBOW;
}

static bool is_valid_gpio(int gpio)
{
    return gpio >= 0 && GPIO_IS_VALID_OUTPUT_GPIO(gpio);
}

static esp_err_t write_channel(size_t index, uint8_t value,
                               uint8_t brightness, uint8_t pulse)
{
    uint32_t scaled = (uint32_t)value * brightness * pulse / 10000u;
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

static esp_err_t write_rgb(uint8_t red, uint8_t green, uint8_t blue,
                           uint8_t brightness, uint8_t pulse)
{
    ESP_RETURN_ON_ERROR(write_channel(0, red, brightness, pulse), TAG,
                        "failed to update red output");
    ESP_RETURN_ON_ERROR(write_channel(1, green, brightness, pulse), TAG,
                        "failed to update green output");
    return write_channel(2, blue, brightness, pulse);
}

static uint8_t triangle_pulse(uint32_t now_ms, uint32_t period_ms,
                              uint8_t minimum)
{
    uint32_t phase;
    uint32_t wave;

    if (period_ms == 0u) {
        return 100u;
    }
    phase = now_ms % period_ms;
    wave = phase <= period_ms / 2u
               ? phase * 2u
               : (period_ms - phase) * 2u;
    wave = wave * 100u / period_ms;
    return (uint8_t)(minimum + wave * (100u - minimum) / 100u);
}

static void hsv_to_rgb(uint8_t hue, uint8_t *red, uint8_t *green,
                       uint8_t *blue)
{
    const uint8_t region = hue / 43u;
    const uint8_t remainder = (uint8_t)((hue - region * 43u) * 6u);
    const uint8_t p = 0u;
    const uint8_t q = (uint8_t)(255u - remainder);
    const uint8_t t = remainder;

    switch (region) {
    case 0:
        *red = 255u;
        *green = t;
        *blue = p;
        break;
    case 1:
        *red = q;
        *green = 255u;
        *blue = p;
        break;
    case 2:
        *red = p;
        *green = 255u;
        *blue = t;
        break;
    case 3:
        *red = p;
        *green = q;
        *blue = 255u;
        break;
    case 4:
        *red = t;
        *green = p;
        *blue = 255u;
        break;
    default:
        *red = 255u;
        *green = p;
        *blue = q;
        break;
    }
}

static void render_state(void)
{
    rgb_indicator_snapshot_t state;
    uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    uint8_t red = 0u;
    uint8_t green = 0u;
    uint8_t blue = 0u;
    uint8_t brightness = 0u;
    uint8_t pulse = 100u;

    taskENTER_CRITICAL(&s_state_mux);
    state = s_state;
    taskEXIT_CRITICAL(&s_state_mux);

    switch (state.mode) {
    case RGB_INDICATOR_MODE_OFF:
        break;
    case RGB_INDICATOR_MODE_SOLID:
        red = state.red;
        green = state.green;
        blue = state.blue;
        brightness = state.brightness;
        break;
    case RGB_INDICATOR_MODE_RAINBOW:
        hsv_to_rgb(s_rainbow_hue, &red, &green, &blue);
        s_rainbow_hue = (uint8_t)(s_rainbow_hue + 2u);
        brightness = state.brightness;
        break;
    case RGB_INDICATOR_MODE_AUTO:
    default:
        if (state.wellness_alert) {
            red = 255u;
            green = 28u;
            brightness = 85u;
            pulse = ((now_ms / 260u) % 2u) == 0u ? 100u : 0u;
        } else if (state.water_due) {
            red = 0u;
            green = 150u;
            blue = 255u;
            brightness = 55u;
            pulse = triangle_pulse(now_ms, 900u, 12u);
        } else if (state.wellness_active && state.wellness_present) {
            red = 0u;
            green = 225u;
            blue = 255u;
            brightness = 38u;
            pulse = triangle_pulse(now_ms, 3000u, 22u);
        } else if (state.wellness_active) {
            red = 0u;
            green = 255u;
            blue = 96u;
            brightness = 50u;
            pulse = triangle_pulse(now_ms, 1800u, 25u);
        } else {
            red = 72u;
            green = 24u;
            blue = 210u;
            brightness = 12u;
        }
        break;
    }

    esp_err_t err = write_rgb(red, green, blue, brightness, pulse);
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

static void load_persisted_state(void)
{
    rgb_persisted_config_t persisted = {0};
    size_t size = sizeof(persisted);
    bool found = false;
    esp_err_t err;

    err = config_service_load_blob(CONFIG_SERVICE_ITEM_RGB_SETTINGS,
                                   &persisted, &size, &found);
    if (err != ESP_OK || !found || size != sizeof(persisted) ||
        persisted.magic != RGB_CONFIG_MAGIC ||
        persisted.version != RGB_CONFIG_VERSION ||
        !is_valid_mode((rgb_indicator_mode_t)persisted.mode) ||
        persisted.brightness > 100u) {
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "saved RGB mode unavailable: %s",
                     esp_err_to_name(err));
        }
        return;
    }

    s_state.mode = (rgb_indicator_mode_t)persisted.mode;
    s_state.red = persisted.red;
    s_state.green = persisted.green;
    s_state.blue = persisted.blue;
    s_state.brightness = persisted.brightness;
    ESP_LOGI(TAG, "restored RGB mode %s",
             rgb_indicator_mode_name(s_state.mode));
}

static void stop_outputs(void)
{
    for (size_t index = 0; index < sizeof(s_channels) / sizeof(s_channels[0]);
         index++) {
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
    memset(&s_state, 0, sizeof(s_state));
    s_state.enabled = true;
    s_state.mode = RGB_INDICATOR_MODE_AUTO;
    s_state.red = 0u;
    s_state.green = 229u;
    s_state.blue = 255u;
    s_state.brightness = 45u;
    load_persisted_state();

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
    for (size_t index = 0; index < sizeof(s_channels) / sizeof(s_channels[0]);
         index++) {
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
    s_initialized = true;
    s_state.ready = true;
    if (xTaskCreate(rgb_indicator_task, "rgb_indicator",
                    RGB_INDICATOR_TASK_STACK, NULL,
                    RGB_INDICATOR_TASK_PRIORITY, &s_task) != pdPASS) {
        s_state.ready = false;
        stop_outputs();
        s_initialized = false;
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    ESP_LOGI(TAG, "rgb indicator ready on R=%d G=%d B=%d",
             s_config.red_gpio, s_config.green_gpio, s_config.blue_gpio);
    return ESP_OK;
}

esp_err_t rgb_indicator_set_mode(rgb_indicator_mode_t mode, uint8_t red,
                                 uint8_t green, uint8_t blue,
                                 uint8_t brightness)
{
    rgb_persisted_config_t persisted = {
        .magic = RGB_CONFIG_MAGIC,
        .version = RGB_CONFIG_VERSION,
        .mode = (uint8_t)mode,
        .red = red,
        .green = green,
        .blue = blue,
        .brightness = brightness,
        .reserved = 0u,
    };
    esp_err_t err;

    if (!is_valid_mode(mode) || brightness > 100u) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_started || !s_state.ready) {
        return ESP_ERR_INVALID_STATE;
    }

    err = config_service_save_blob(CONFIG_SERVICE_ITEM_RGB_SETTINGS,
                                   &persisted, sizeof(persisted));
    if (err != ESP_OK) {
        return err;
    }

    taskENTER_CRITICAL(&s_state_mux);
    s_state.mode = mode;
    s_state.red = red;
    s_state.green = green;
    s_state.blue = blue;
    s_state.brightness = brightness;
    taskEXIT_CRITICAL(&s_state_mux);
    return ESP_OK;
}

void rgb_indicator_set_wellness_state(bool active, bool present, bool alert,
                                      bool water_due)
{
    if (!s_initialized) {
        return;
    }

    taskENTER_CRITICAL(&s_state_mux);
    s_state.wellness_active = active;
    s_state.wellness_present = present;
    s_state.wellness_alert = alert;
    s_state.water_due = water_due;
    taskEXIT_CRITICAL(&s_state_mux);
}

bool rgb_indicator_get_snapshot(rgb_indicator_snapshot_t *snapshot)
{
    if (!s_initialized || snapshot == NULL) {
        return false;
    }

    taskENTER_CRITICAL(&s_state_mux);
    *snapshot = s_state;
    taskEXIT_CRITICAL(&s_state_mux);
    return true;
}

const char *rgb_indicator_mode_name(rgb_indicator_mode_t mode)
{
    switch (mode) {
    case RGB_INDICATOR_MODE_OFF:
        return "off";
    case RGB_INDICATOR_MODE_SOLID:
        return "solid";
    case RGB_INDICATOR_MODE_RAINBOW:
        return "rainbow";
    case RGB_INDICATOR_MODE_AUTO:
    default:
        return "auto";
    }
}
