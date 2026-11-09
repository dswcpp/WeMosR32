#include "vibration_service.h"

#include <stddef.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "feedback_player.h"
#include "freertos/FreeRTOS.h"

#define VIBRATION_TICK_US 10000u

static const char *TAG = "vibration";

static const feedback_pulse_t WATER_PULSES[] = {
    {220u, 180u},
    {220u, 0u},
};
static const feedback_pulse_t SEDENTARY_PULSES[] = {
    {300u, 200u},
    {300u, 0u},
};
static const feedback_pulse_t ESCALATED_PULSES[] = {
    {350u, 180u},
    {350u, 180u},
    {350u, 0u},
};
static const feedback_pulse_t CONFIRM_PULSES[] = {
    {100u, 0u},
};

static const feedback_pattern_t s_patterns[] = {
    [VIBRATION_PATTERN_NONE] = {NULL, 0u},
    [VIBRATION_PATTERN_WATER_REMINDER] = {
        WATER_PULSES, (uint8_t)(sizeof(WATER_PULSES) / sizeof(WATER_PULSES[0]))
    },
    [VIBRATION_PATTERN_SEDENTARY_ALERT] = {
        SEDENTARY_PULSES,
        (uint8_t)(sizeof(SEDENTARY_PULSES) / sizeof(SEDENTARY_PULSES[0]))
    },
    [VIBRATION_PATTERN_SEDENTARY_ESCALATED] = {
        ESCALATED_PULSES,
        (uint8_t)(sizeof(ESCALATED_PULSES) / sizeof(ESCALATED_PULSES[0]))
    },
    [VIBRATION_PATTERN_CONFIRM] = {
        CONFIRM_PULSES, (uint8_t)(sizeof(CONFIRM_PULSES) /
                                  sizeof(CONFIRM_PULSES[0]))
    },
};

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static feedback_player_t s_player;
static esp_timer_handle_t s_timer;
static int s_gpio = -1;
static vibration_pattern_t s_pattern;
static uint32_t s_play_count;
static bool s_active_high = true;
static bool s_ready;

static int output_level(bool on)
{
    if (s_active_high) {
        return on ? 1 : 0;
    }
    return on ? 0 : 1;
}

static void apply_player_state(void)
{
    bool output_on;

    taskENTER_CRITICAL(&s_mux);
    feedback_player_update(&s_player, (uint64_t)esp_timer_get_time());
    output_on = s_player.output_on;
    taskEXIT_CRITICAL(&s_mux);

    (void)gpio_set_level((gpio_num_t)s_gpio, output_level(output_on));
}

static void vibration_timer_callback(void *argument)
{
    (void)argument;
    apply_player_state();
}

static bool pattern_valid(vibration_pattern_t pattern)
{
    return pattern >= VIBRATION_PATTERN_NONE &&
           pattern <= VIBRATION_PATTERN_CONFIRM;
}

esp_err_t vibration_service_start(
    const vibration_service_config_t *config)
{
    esp_err_t err;
    gpio_config_t gpio_cfg;
    const esp_timer_create_args_t timer_args = {
        .callback = vibration_timer_callback,
        .name = "vibration",
    };

    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ready) {
        return ESP_OK;
    }
    if (!config->enabled) {
        ESP_LOGI(TAG, "disabled");
        return ESP_OK;
    }
    if (config->gpio < 0 ||
        !GPIO_IS_VALID_OUTPUT_GPIO(config->gpio)) {
        return ESP_ERR_INVALID_ARG;
    }

    gpio_cfg = (gpio_config_t) {
        .pin_bit_mask = 1ULL << config->gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    s_gpio = config->gpio;
    s_active_high = config->active_high;
    err = gpio_config(&gpio_cfg);
    if (err != ESP_OK) {
        return err;
    }
    err = gpio_set_level((gpio_num_t)s_gpio, output_level(false));
    if (err != ESP_OK) {
        return err;
    }
    err = esp_timer_create(&timer_args, &s_timer);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_timer_start_periodic(s_timer, VIBRATION_TICK_US);
    if (err != ESP_OK) {
        (void)esp_timer_delete(s_timer);
        s_timer = NULL;
        return err;
    }

    s_ready = true;
    ESP_LOGI(TAG, "ready on GPIO%d (%s)", s_gpio,
             s_active_high ? "active high" : "active low");
    return ESP_OK;
}

esp_err_t vibration_service_play(vibration_pattern_t pattern)
{
    if (!pattern_valid(pattern)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (pattern == VIBRATION_PATTERN_NONE) {
        vibration_service_stop();
        return ESP_OK;
    }

    taskENTER_CRITICAL(&s_mux);
    s_pattern = pattern;
    s_play_count++;
    feedback_player_start(&s_player, &s_patterns[pattern],
                          (uint64_t)esp_timer_get_time());
    taskEXIT_CRITICAL(&s_mux);
    apply_player_state();
    return ESP_OK;
}

void vibration_service_stop(void)
{
    if (!s_ready) {
        return;
    }

    taskENTER_CRITICAL(&s_mux);
    s_pattern = VIBRATION_PATTERN_NONE;
    feedback_player_stop(&s_player);
    taskEXIT_CRITICAL(&s_mux);
    (void)gpio_set_level((gpio_num_t)s_gpio, output_level(false));
}

bool vibration_service_is_ready(void)
{
    return s_ready;
}

bool vibration_service_get_snapshot(vibration_service_snapshot_t *snapshot)
{
    feedback_player_snapshot_t player_snapshot;

    if (snapshot == NULL) {
        return false;
    }
    *snapshot = (vibration_service_snapshot_t){0};
    if (!s_ready) {
        return false;
    }

    taskENTER_CRITICAL(&s_mux);
    feedback_player_get_snapshot(&s_player,
                                 (uint64_t)esp_timer_get_time(),
                                 &player_snapshot);
    snapshot->ready = true;
    snapshot->active = player_snapshot.active;
    snapshot->vibrating = player_snapshot.output_on;
    snapshot->pattern = s_pattern;
    snapshot->pulse_count = player_snapshot.pulse_count;
    snapshot->pulse_index = player_snapshot.pulse_index;
    snapshot->remaining_ms = player_snapshot.remaining_ms;
    snapshot->play_count = s_play_count;
    taskEXIT_CRITICAL(&s_mux);
    return true;
}

uint8_t vibration_service_pattern_pulse_count(vibration_pattern_t pattern)
{
    return pattern_valid(pattern) ? s_patterns[pattern].pulse_count : 0u;
}

uint32_t vibration_service_pattern_duration_ms(vibration_pattern_t pattern)
{
    return pattern_valid(pattern)
               ? feedback_pattern_duration_ms(&s_patterns[pattern])
               : 0u;
}
