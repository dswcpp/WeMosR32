#include "buzzer_service.h"

#include <stddef.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "feedback_player.h"
#include "freertos/FreeRTOS.h"

#define BUZZER_TICK_US 10000u

static const char *TAG = "buzzer";

static const feedback_pulse_t NAVIGATE_PULSES[] = {
    {30u, 0u},
};
static const feedback_pulse_t REFRESH_SUCCESS_PULSES[] = {
    {60u, 80u},
    {60u, 0u},
};
static const feedback_pulse_t REFRESH_FAILED_PULSES[] = {
    {100u, 90u},
    {100u, 90u},
    {100u, 0u},
};
static const feedback_pulse_t PRICE_ALERT_PULSES[] = {
    {120u, 100u},
    {120u, 0u},
};

static const feedback_pattern_t s_patterns[] = {
    [BUZZER_PATTERN_NONE] = {NULL, 0u},
    [BUZZER_PATTERN_NAVIGATE] = {
        NAVIGATE_PULSES,
        (uint8_t)(sizeof(NAVIGATE_PULSES) / sizeof(NAVIGATE_PULSES[0]))
    },
    [BUZZER_PATTERN_REFRESH_SUCCESS] = {
        REFRESH_SUCCESS_PULSES,
        (uint8_t)(sizeof(REFRESH_SUCCESS_PULSES) /
                  sizeof(REFRESH_SUCCESS_PULSES[0]))
    },
    [BUZZER_PATTERN_REFRESH_FAILED] = {
        REFRESH_FAILED_PULSES,
        (uint8_t)(sizeof(REFRESH_FAILED_PULSES) /
                  sizeof(REFRESH_FAILED_PULSES[0]))
    },
    [BUZZER_PATTERN_PRICE_ALERT] = {
        PRICE_ALERT_PULSES,
        (uint8_t)(sizeof(PRICE_ALERT_PULSES) /
                  sizeof(PRICE_ALERT_PULSES[0]))
    },
};

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static feedback_player_t s_player;
static esp_timer_handle_t s_timer;
static int s_gpio = -1;
static buzzer_pattern_t s_pattern;
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

static void buzzer_timer_callback(void *argument)
{
    (void)argument;
    apply_player_state();
}

static bool pattern_valid(buzzer_pattern_t pattern)
{
    return pattern >= BUZZER_PATTERN_NONE &&
           pattern <= BUZZER_PATTERN_PRICE_ALERT;
}

esp_err_t buzzer_service_start(const buzzer_service_config_t *config)
{
    esp_err_t err;
    gpio_config_t gpio_cfg;
    const esp_timer_create_args_t timer_args = {
        .callback = buzzer_timer_callback,
        .name = "buzzer",
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
    err = esp_timer_start_periodic(s_timer, BUZZER_TICK_US);
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

esp_err_t buzzer_service_play(buzzer_pattern_t pattern)
{
    if (!pattern_valid(pattern)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (pattern == BUZZER_PATTERN_NONE) {
        buzzer_service_stop();
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

void buzzer_service_stop(void)
{
    if (!s_ready) {
        return;
    }

    taskENTER_CRITICAL(&s_mux);
    s_pattern = BUZZER_PATTERN_NONE;
    feedback_player_stop(&s_player);
    taskEXIT_CRITICAL(&s_mux);
    (void)gpio_set_level((gpio_num_t)s_gpio, output_level(false));
}

bool buzzer_service_is_ready(void)
{
    return s_ready;
}

bool buzzer_service_get_snapshot(buzzer_service_snapshot_t *snapshot)
{
    feedback_player_snapshot_t player_snapshot;

    if (snapshot == NULL) {
        return false;
    }
    *snapshot = (buzzer_service_snapshot_t){0};
    if (!s_ready) {
        return false;
    }

    taskENTER_CRITICAL(&s_mux);
    feedback_player_get_snapshot(&s_player,
                                 (uint64_t)esp_timer_get_time(),
                                 &player_snapshot);
    snapshot->ready = true;
    snapshot->active = player_snapshot.active;
    snapshot->beeping = player_snapshot.output_on;
    snapshot->pattern = s_pattern;
    snapshot->beep_count = player_snapshot.pulse_count;
    snapshot->beep_index = player_snapshot.pulse_index;
    snapshot->remaining_ms = player_snapshot.remaining_ms;
    snapshot->play_count = s_play_count;
    taskEXIT_CRITICAL(&s_mux);
    return true;
}

uint8_t buzzer_service_pattern_beep_count(buzzer_pattern_t pattern)
{
    return pattern_valid(pattern) ? s_patterns[pattern].pulse_count : 0u;
}

uint32_t buzzer_service_pattern_duration_ms(buzzer_pattern_t pattern)
{
    return pattern_valid(pattern)
               ? feedback_pattern_duration_ms(&s_patterns[pattern])
               : 0u;
}
