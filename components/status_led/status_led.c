#include "status_led.h"

#include <stddef.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define STATUS_LED_TASK_STACK 2048u
#define STATUS_LED_TASK_PRIORITY 2u
#define STATUS_LED_TICK_MS 10u

static const char *TAG = "status_led";
static status_led_config_t s_config;
static portMUX_TYPE s_mode_mux = portMUX_INITIALIZER_UNLOCKED;
static status_led_mode_t s_mode;
static TaskHandle_t s_task;
static bool s_initialized;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static int output_level(bool on)
{
    bool high = s_config.active_high ? on : !on;
    return high ? 1 : 0;
}

static esp_err_t write_output(bool on)
{
    return gpio_set_level((gpio_num_t)s_config.gpio, output_level(on));
}

static status_led_mode_t current_mode(void)
{
    status_led_mode_t mode;

    taskENTER_CRITICAL(&s_mode_mux);
    mode = s_mode;
    taskEXIT_CRITICAL(&s_mode_mux);
    return mode;
}

static void status_led_task(void *argument)
{
    TickType_t last_wake = xTaskGetTickCount();
    status_led_mode_t rendered_mode = STATUS_LED_MODE_COUNT;
    uint32_t mode_started_ms = 0u;
    int rendered_level = -1;

    (void)argument;
    while (true) {
        status_led_mode_t mode = current_mode();
        uint32_t current_ms = now_ms();
        int level;

        if (mode != rendered_mode) {
            rendered_mode = mode;
            mode_started_ms = current_ms;
        }

        level = output_level(
            status_led_pattern_on(mode, current_ms - mode_started_ms));
        if (level != rendered_level) {
            if (gpio_set_level((gpio_num_t)s_config.gpio, level) != ESP_OK) {
                ESP_LOGW(TAG, "GPIO%d output update failed",
                         s_config.gpio);
            }
            rendered_level = level;
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(STATUS_LED_TICK_MS));
    }
}

esp_err_t status_led_service_start(const status_led_config_t *config)
{
    gpio_config_t gpio_cfg = {
        .pin_bit_mask = 0,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err;

    if (s_initialized || config == NULL ||
        !GPIO_IS_VALID_OUTPUT_GPIO(config->gpio)) {
        return ESP_ERR_INVALID_ARG;
    }

    s_config = *config;
    taskENTER_CRITICAL(&s_mode_mux);
    s_mode = STATUS_LED_MODE_BOOTING;
    taskEXIT_CRITICAL(&s_mode_mux);

    gpio_cfg.pin_bit_mask = 1ULL << config->gpio;
    err = gpio_config(&gpio_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO%d configuration failed: %s",
                 config->gpio, esp_err_to_name(err));
        return err;
    }

    err = write_output(false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO%d initial output failed: %s",
                 config->gpio, esp_err_to_name(err));
        gpio_reset_pin((gpio_num_t)config->gpio);
        return err;
    }

    if (xTaskCreate(status_led_task, "status_led",
                    STATUS_LED_TASK_STACK, NULL,
                    STATUS_LED_TASK_PRIORITY, &s_task) != pdPASS) {
        (void)write_output(false);
        gpio_reset_pin((gpio_num_t)config->gpio);
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    ESP_LOGI(TAG,
             "ready on GPIO%d (%s), boot triple-blink then heartbeat",
             s_config.gpio,
             s_config.active_high ? "active high" : "active low");
    return ESP_OK;
}

void status_led_service_set_mode(status_led_mode_t mode)
{
    if (!s_initialized || mode < STATUS_LED_MODE_OFF ||
        mode >= STATUS_LED_MODE_COUNT) {
        return;
    }

    taskENTER_CRITICAL(&s_mode_mux);
    s_mode = mode;
    taskEXIT_CRITICAL(&s_mode_mux);
}
