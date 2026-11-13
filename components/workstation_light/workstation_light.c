#include "workstation_light.h"

#include <stddef.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "workstation_light";

#define PRESENCE_POLL_MS 500u
#define PRESENCE_TASK_STACK 3072u
#define PRESENCE_TASK_PRIORITY 3u

typedef enum {
    RELAY_POWER = 0,
    RELAY_BRIGHTNESS,
    RELAY_COLOR,
    RELAY_COUNT,
} relay_index_t;

static workstation_light_config_t s_config;
static workstation_light_snapshot_t s_state;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t s_action_lock;
static TaskHandle_t s_presence_task;
static bool s_initialized;

static esp_err_t execute_action(workstation_light_action_t action,
                                bool automatic);

static bool is_valid_output_gpio(int gpio)
{
    return gpio >= 0 && GPIO_IS_VALID_OUTPUT_GPIO(gpio);
}

static int gpio_level_for(bool active)
{
    if (s_config.active_low) {
        return active ? 0 : 1;
    }
    return active ? 1 : 0;
}

static int presence_led_level_for(bool active)
{
    if (s_config.presence_led_active_low) {
        return active ? 0 : 1;
    }
    return active ? 1 : 0;
}

static int relay_gpio(relay_index_t relay)
{
    switch (relay) {
    case RELAY_POWER:
        return s_config.power_gpio;
    case RELAY_BRIGHTNESS:
        return s_config.brightness_gpio;
    case RELAY_COLOR:
        return s_config.color_gpio;
    default:
        return -1;
    }
}

static esp_err_t set_relay(relay_index_t relay, bool active)
{
    const int gpio = relay_gpio(relay);

    if (gpio < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return gpio_set_level((gpio_num_t)gpio, gpio_level_for(active));
}

static TickType_t delay_ticks(uint32_t milliseconds)
{
    TickType_t ticks = pdMS_TO_TICKS(milliseconds);

    return ticks == 0 ? 1 : ticks;
}

static void set_busy(bool busy)
{
    taskENTER_CRITICAL(&s_state_lock);
    s_state.busy = busy;
    taskEXIT_CRITICAL(&s_state_lock);
}

static esp_err_t pulse_relay(relay_index_t relay)
{
    esp_err_t err;

    err = set_relay(relay, true);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(delay_ticks(s_config.pulse_ms));

    err = set_relay(relay, false);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(delay_ticks(s_config.settle_ms));
    return ESP_OK;
}

static esp_err_t request_power(bool power_on)
{
    /* Relay 1 emulates the lamp's power button, so it pulses on changes. */
    if (s_state.power_on == power_on) {
        return ESP_OK;
    }
    return pulse_relay(RELAY_POWER);
}

static esp_err_t configure_outputs(void)
{
    const int gpios[RELAY_COUNT] = {
        s_config.power_gpio,
        s_config.brightness_gpio,
        s_config.color_gpio,
    };
    const gpio_config_t output_config = {
        .pin_bit_mask = (1ULL << gpios[0]) |
                        (1ULL << gpios[1]) |
                        (1ULL << gpios[2]),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err;

    for (size_t index = 0u; index < RELAY_COUNT; index++) {
        err = gpio_set_level((gpio_num_t)gpios[index],
                             gpio_level_for(false));
        if (err != ESP_OK) {
            return err;
        }
    }

    err = gpio_config(&output_config);
    if (err != ESP_OK) {
        return err;
    }

    for (size_t index = 0u; index < RELAY_COUNT; index++) {
        err = set_relay((relay_index_t)index, false);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

static esp_err_t configure_presence_led(void)
{
    const gpio_config_t output_config = {
        .pin_bit_mask = 1ULL << s_config.presence_led_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err;

    err = gpio_set_level(
        (gpio_num_t)s_config.presence_led_gpio,
        presence_led_level_for(false));
    if (err != ESP_OK) {
        return err;
    }
    err = gpio_config(&output_config);
    if (err != ESP_OK) {
        return err;
    }
    return gpio_set_level(
        (gpio_num_t)s_config.presence_led_gpio,
        presence_led_level_for(false));
}

static void update_presence_state(bool known, bool present)
{
    const bool led_on =
        s_config.presence_led_enabled && known && present;

    if (s_config.presence_led_enabled) {
        esp_err_t err = gpio_set_level(
            (gpio_num_t)s_config.presence_led_gpio,
            presence_led_level_for(led_on));

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "presence LED update failed: %s",
                     esp_err_to_name(err));
        }
    }

    taskENTER_CRITICAL(&s_state_lock);
    s_state.presence_known = known;
    s_state.presence = known && present;
    s_state.presence_led_on = led_on;
    taskEXIT_CRITICAL(&s_state_lock);
}

static void presence_task(void *context)
{
    bool have_last = false;
    bool last_present = false;

    (void)context;
    for (;;) {
        bool present = false;

        if (s_config.presence_provider != NULL &&
            s_config.presence_provider(&present, s_config.presence_context)) {
            update_presence_state(true, present);
            if (s_config.presence_auto &&
                (!have_last || present != last_present)) {
                workstation_light_action_t action =
                    present ? WORKSTATION_LIGHT_ACTION_POWER_ON
                            : WORKSTATION_LIGHT_ACTION_POWER_OFF;
                esp_err_t err = execute_action(action, true);

                if (err != ESP_OK) {
                    ESP_LOGW(TAG,
                             "automatic presence-%s failed: %s",
                             present ? "on" : "off",
                             esp_err_to_name(err));
                } else {
                    have_last = true;
                    last_present = present;
                }
            }
        } else {
            update_presence_state(false, false);
        }

        vTaskDelay(delay_ticks(PRESENCE_POLL_MS));
    }
}

esp_err_t workstation_light_init(
    const workstation_light_config_t *config)
{
    esp_err_t err;

    if (s_initialized) {
        return s_state.ready ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    if (config == NULL || !config->enabled ||
        config->pulse_ms == 0u || config->settle_ms == 0u ||
        ((config->presence_auto || config->presence_led_enabled) &&
         config->presence_provider == NULL) ||
        !is_valid_output_gpio(config->power_gpio) ||
        !is_valid_output_gpio(config->brightness_gpio) ||
        !is_valid_output_gpio(config->color_gpio) ||
        config->power_gpio == config->brightness_gpio ||
        config->power_gpio == config->color_gpio ||
        config->brightness_gpio == config->color_gpio) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->presence_led_enabled &&
        (!is_valid_output_gpio(config->presence_led_gpio) ||
         config->presence_led_gpio == config->power_gpio ||
         config->presence_led_gpio == config->brightness_gpio ||
         config->presence_led_gpio == config->color_gpio)) {
        return ESP_ERR_INVALID_ARG;
    }

    s_action_lock = xSemaphoreCreateMutex();
    if (s_action_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_config = *config;
    memset(&s_state, 0, sizeof(s_state));
    s_state.enabled = true;
    s_state.ready = true;
    s_state.active_low = config->active_low;
    s_state.pulse_ms = config->pulse_ms;
    s_state.settle_ms = config->settle_ms;
    s_state.last_action = WORKSTATION_LIGHT_ACTION_POWER_OFF;
    s_state.presence_auto = config->presence_auto;
    s_state.presence_led_enabled = config->presence_led_enabled;
    s_state.presence_led_gpio =
        config->presence_led_enabled ? config->presence_led_gpio : -1;
    s_initialized = true;

    err = configure_outputs();
    if (err != ESP_OK) {
        s_state.ready = false;
        s_initialized = false;
        vSemaphoreDelete(s_action_lock);
        s_action_lock = NULL;
        return err;
    }

    if (s_config.presence_led_enabled) {
        err = configure_presence_led();
        if (err != ESP_OK) {
            ESP_LOGW(TAG,
                     "presence LED unavailable on GPIO%d; "
                     "continuing without it: %s",
                     s_config.presence_led_gpio, esp_err_to_name(err));
            s_config.presence_led_enabled = false;
            s_state.presence_led_enabled = false;
            s_state.presence_led_gpio = -1;
        }
    }

    if (s_config.presence_auto || s_config.presence_led_enabled) {
        if (xTaskCreate(presence_task, "workstation_presence",
                        PRESENCE_TASK_STACK, NULL, PRESENCE_TASK_PRIORITY,
                        &s_presence_task) != pdPASS) {
            s_state.presence_auto = false;
            s_config.presence_auto = false;
            s_state.presence_led_enabled = false;
            s_state.presence_led_gpio = -1;
            s_config.presence_led_enabled = false;
            ESP_LOGW(TAG,
                     "presence automation task unavailable; "
                     "manual control remains enabled");
        }
    }

    ESP_LOGI(TAG,
             "relay controller ready: power=GPIO%d brightness=GPIO%d "
             "color=GPIO%d active_low=%d presence_auto=%d "
             "presence_led=%d",
             s_config.power_gpio, s_config.brightness_gpio,
             s_config.color_gpio, s_config.active_low,
             s_config.presence_auto, s_config.presence_led_enabled);
    return ESP_OK;
}

static esp_err_t execute_action(workstation_light_action_t action,
                                bool automatic)
{
    esp_err_t err;
    bool power_on = false;
    uint32_t brightness_cycles = 0u;
    uint32_t color_cycles = 0u;

    if (!s_initialized || !s_state.ready || s_action_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (action < WORKSTATION_LIGHT_ACTION_POWER_OFF ||
        action > WORKSTATION_LIGHT_ACTION_COLOR_NEXT) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(
            s_action_lock,
            delay_ticks(s_config.pulse_ms + s_config.settle_ms + 1000u)) !=
        pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    set_busy(true);
    switch (action) {
    case WORKSTATION_LIGHT_ACTION_POWER_ON:
        power_on = true;
        err = request_power(power_on);
        break;
    case WORKSTATION_LIGHT_ACTION_POWER_OFF:
        power_on = false;
        err = request_power(power_on);
        break;
    case WORKSTATION_LIGHT_ACTION_TOGGLE_POWER:
        power_on = !s_state.power_on;
        err = request_power(power_on);
        break;
    case WORKSTATION_LIGHT_ACTION_BRIGHTNESS_NEXT:
        err = pulse_relay(RELAY_BRIGHTNESS);
        break;
    case WORKSTATION_LIGHT_ACTION_COLOR_NEXT:
        err = pulse_relay(RELAY_COLOR);
        break;
    default:
        err = ESP_ERR_INVALID_ARG;
        break;
    }

    if (err == ESP_OK) {
        taskENTER_CRITICAL(&s_state_lock);
        if (action == WORKSTATION_LIGHT_ACTION_POWER_ON ||
            action == WORKSTATION_LIGHT_ACTION_POWER_OFF ||
            action == WORKSTATION_LIGHT_ACTION_TOGGLE_POWER) {
            s_state.power_on = power_on;
        } else if (action == WORKSTATION_LIGHT_ACTION_BRIGHTNESS_NEXT) {
            brightness_cycles = ++s_state.brightness_cycles;
        } else if (action == WORKSTATION_LIGHT_ACTION_COLOR_NEXT) {
            color_cycles = ++s_state.color_cycles;
        }
        s_state.last_action = action;
        s_state.last_action_automatic = automatic;
        taskEXIT_CRITICAL(&s_state_lock);
    }

    set_busy(false);
    xSemaphoreGive(s_action_lock);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "action=%s source=%s power=%d brightness_cycles=%lu "
                      "color_cycles=%lu",
                 workstation_light_action_name(action),
                 automatic ? "presence" : "manual",
                 s_state.power_on,
                 (unsigned long)brightness_cycles,
                 (unsigned long)color_cycles);
    } else {
        ESP_LOGE(TAG, "action=%s failed: %s",
                 workstation_light_action_name(action),
                 esp_err_to_name(err));
    }
    return err;
}

esp_err_t workstation_light_execute(workstation_light_action_t action)
{
    return execute_action(action, false);
}

bool workstation_light_get_snapshot(workstation_light_snapshot_t *snapshot)
{
    if (!s_initialized || !s_state.ready || snapshot == NULL) {
        return false;
    }

    taskENTER_CRITICAL(&s_state_lock);
    *snapshot = s_state;
    taskEXIT_CRITICAL(&s_state_lock);
    return true;
}

const char *workstation_light_action_name(workstation_light_action_t action)
{
    switch (action) {
    case WORKSTATION_LIGHT_ACTION_POWER_ON:
        return "power_on";
    case WORKSTATION_LIGHT_ACTION_TOGGLE_POWER:
        return "toggle_power";
    case WORKSTATION_LIGHT_ACTION_BRIGHTNESS_NEXT:
        return "brightness_next";
    case WORKSTATION_LIGHT_ACTION_COLOR_NEXT:
        return "color_next";
    case WORKSTATION_LIGHT_ACTION_POWER_OFF:
    default:
        return "power_off";
    }
}
