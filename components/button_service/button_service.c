#include "button_service.h"

#include <stddef.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define BUTTON_SERVICE_SCAN_MS 10u
#define BUTTON_SERVICE_TASK_STACK 2560u
#define BUTTON_SERVICE_TASK_PRIORITY 4u
#define BUTTON_SERVICE_QUEUE_LENGTH 16u

static const char *TAG = "buttons";

typedef struct {
    bool raw_pressed;
    bool stable_pressed;
    bool long_sent;
    uint64_t changed_at_us;
    uint64_t pressed_at_us;
} button_state_t;

static button_service_config_t s_config;
static button_state_t s_states[BUTTON_SERVICE_COUNT];
static uint32_t s_short_presses[BUTTON_SERVICE_COUNT];
static uint32_t s_long_presses[BUTTON_SERVICE_COUNT];
static portMUX_TYPE s_state_mux = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t s_event_queue;
static TaskHandle_t s_task;
static bool s_ready;

static bool gpio_is_valid_input(int gpio)
{
    return gpio >= 0 && GPIO_IS_VALID_GPIO(gpio);
}

static bool read_pressed(int gpio)
{
    const bool level = gpio_get_level((gpio_num_t)gpio) != 0;

    return s_config.active_low ? !level : level;
}

static bool publish_event(button_id_t button, button_event_type_t type,
                          uint32_t held_ms)
{
    const button_event_t event = {
        .button = button,
        .type = type,
        .held_ms = held_ms,
    };

    if (s_event_queue == NULL ||
        xQueueSend(s_event_queue, &event, 0) != pdTRUE) {
        return false;
    }

    taskENTER_CRITICAL(&s_state_mux);
    if (type == BUTTON_EVENT_SHORT_PRESS) {
        s_short_presses[button]++;
    } else {
        s_long_presses[button]++;
    }
    taskEXIT_CRITICAL(&s_state_mux);
    return true;
}

static bool update_button(button_id_t button, uint64_t now_us,
                          button_event_t *event)
{
    button_state_t *state = &s_states[button];
    const bool pressed = read_pressed(s_config.gpio[button]);
    bool emit_short = false;

    taskENTER_CRITICAL(&s_state_mux);
    if (pressed != state->raw_pressed) {
        state->raw_pressed = pressed;
        state->changed_at_us = now_us;
    }

    if (state->raw_pressed == state->stable_pressed ||
        now_us - state->changed_at_us <
            (uint64_t)s_config.debounce_ms * 1000u) {
        taskEXIT_CRITICAL(&s_state_mux);
        return false;
    }

    state->stable_pressed = state->raw_pressed;
    if (state->stable_pressed) {
        state->pressed_at_us = now_us;
        state->long_sent = false;
    } else if (!state->long_sent) {
        emit_short = true;
        event->button = button;
        event->type = BUTTON_EVENT_SHORT_PRESS;
        event->held_ms =
            (uint32_t)((now_us - state->pressed_at_us) / 1000u);
    }
    taskEXIT_CRITICAL(&s_state_mux);
    return emit_short;
}

static bool take_long_press_event(button_id_t button, uint64_t now_us,
                                  button_event_t *event)
{
    button_state_t *state = &s_states[button];
    bool emit = false;

    taskENTER_CRITICAL(&s_state_mux);
    if (state->stable_pressed && !state->long_sent &&
        now_us - state->pressed_at_us >=
            (uint64_t)s_config.long_press_ms * 1000u) {
        state->long_sent = true;
        event->button = button;
        event->type = BUTTON_EVENT_LONG_PRESS;
        event->held_ms =
            (uint32_t)((now_us - state->pressed_at_us) / 1000u);
        emit = true;
    }
    taskEXIT_CRITICAL(&s_state_mux);
    return emit;
}

static void button_task(void *argument)
{
    TickType_t last_wake = xTaskGetTickCount();

    (void)argument;
    while (true) {
        const uint64_t now_us = (uint64_t)esp_timer_get_time();

        for (size_t index = 0u; index < BUTTON_SERVICE_COUNT; index++) {
            const button_id_t button = (button_id_t)index;
            button_event_t event;

            if (update_button(button, now_us, &event)) {
                (void)publish_event(button, event.type, event.held_ms);
            }
            if (take_long_press_event(button, now_us, &event)) {
                (void)publish_event(button, event.type, event.held_ms);
            }
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(BUTTON_SERVICE_SCAN_MS));
    }
}

esp_err_t button_service_start(const button_service_config_t *config)
{
    esp_err_t err;

    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ready) {
        return ESP_OK;
    }
    if (config->debounce_ms < 10u || config->long_press_ms < 200u) {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t index = 0u; index < BUTTON_SERVICE_COUNT; index++) {
        if (!gpio_is_valid_input(config->gpio[index])) {
            ESP_LOGE(TAG, "K%u uses invalid GPIO%d",
                     (unsigned)(index + 1u), config->gpio[index]);
            return ESP_ERR_INVALID_ARG;
        }
        for (size_t other = 0u; other < index; other++) {
            if (config->gpio[index] == config->gpio[other]) {
                ESP_LOGE(TAG, "K%u and K%u share GPIO%d",
                         (unsigned)(other + 1u), (unsigned)(index + 1u),
                         config->gpio[index]);
                return ESP_ERR_INVALID_ARG;
            }
        }
    }

    s_config = *config;
    memset(s_states, 0, sizeof(s_states));
    memset(s_short_presses, 0, sizeof(s_short_presses));
    memset(s_long_presses, 0, sizeof(s_long_presses));

    for (size_t index = 0u; index < BUTTON_SERVICE_COUNT; index++) {
        const gpio_config_t gpio_cfg = {
            .pin_bit_mask = 1ULL << config->gpio[index],
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = config->active_low ? GPIO_PULLUP_ENABLE
                                             : GPIO_PULLDOWN_ENABLE,
            .pull_down_en = config->active_low ? GPIO_PULLDOWN_DISABLE
                                               : GPIO_PULLUP_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };

        err = gpio_config(&gpio_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "GPIO%d configuration failed: %s",
                     config->gpio[index], esp_err_to_name(err));
            return err;
        }
        s_states[index].raw_pressed = read_pressed(config->gpio[index]);
        s_states[index].stable_pressed = s_states[index].raw_pressed;
        s_states[index].changed_at_us = (uint64_t)esp_timer_get_time();
        s_states[index].pressed_at_us = s_states[index].changed_at_us;
        /* Do not turn a key already held during boot into a synthetic event. */
        s_states[index].long_sent = s_states[index].stable_pressed;
    }

    s_event_queue =
        xQueueCreate(BUTTON_SERVICE_QUEUE_LENGTH, sizeof(button_event_t));
    if (s_event_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(button_task, "buttons", BUTTON_SERVICE_TASK_STACK, NULL,
                    BUTTON_SERVICE_TASK_PRIORITY, &s_task) != pdPASS) {
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_ready = true;
    ESP_LOGI(TAG,
             "ready: K1=GPIO%d K2=GPIO%d K3=GPIO%d K4=GPIO%d (%s)",
             config->gpio[0], config->gpio[1], config->gpio[2],
             config->gpio[3], config->active_low ? "active low"
                                                 : "active high");
    return ESP_OK;
}

esp_err_t button_service_inject_event(button_id_t button,
                                      button_event_type_t type)
{
    uint32_t held_ms;

    if (button < BUTTON_SERVICE_K1 || button >= BUTTON_SERVICE_COUNT ||
        (type != BUTTON_EVENT_SHORT_PRESS &&
         type != BUTTON_EVENT_LONG_PRESS)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready || s_event_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    held_ms = type == BUTTON_EVENT_LONG_PRESS
                  ? s_config.long_press_ms
                  : 0u;
    return publish_event(button, type, held_ms) ? ESP_OK : ESP_ERR_TIMEOUT;
}

bool button_service_read_event(button_event_t *event, TickType_t wait_ticks)
{
    return s_ready && event != NULL && s_event_queue != NULL &&
           xQueueReceive(s_event_queue, event, wait_ticks) == pdTRUE;
}

bool button_service_get_snapshot(button_service_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    if (!s_ready) {
        return false;
    }

    taskENTER_CRITICAL(&s_state_mux);
    snapshot->ready = true;
    for (size_t index = 0u; index < BUTTON_SERVICE_COUNT; index++) {
        snapshot->pressed[index] = s_states[index].stable_pressed;
        snapshot->short_presses[index] = s_short_presses[index];
        snapshot->long_presses[index] = s_long_presses[index];
    }
    taskEXIT_CRITICAL(&s_state_mux);
    return true;
}
