#include "app_events.h"

#include <stdbool.h>
#include <stddef.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define APP_EVENTS_QUEUE_LENGTH 16u
#define APP_EVENTS_TASK_STACK 3072u
#define APP_EVENTS_TASK_PRIORITY 3u

typedef struct {
    app_event_handler_t handler;
    void *context;
} app_event_subscriber_t;

static const char *TAG = "events";

static QueueHandle_t s_queue;
static SemaphoreHandle_t s_subscriber_lock;
static app_event_subscriber_t s_subscribers[APP_EVENTS_MAX_SUBSCRIBERS];
static size_t s_subscriber_count;
static portMUX_TYPE s_sequence_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_next_sequence = 1u;
static TaskHandle_t s_task;
static bool s_initialized;

static void dispatch_event(const app_event_t *event)
{
    app_event_subscriber_t subscribers[APP_EVENTS_MAX_SUBSCRIBERS];
    size_t count = 0u;

    if (xSemaphoreTake(s_subscriber_lock, portMAX_DELAY) == pdTRUE) {
        count = s_subscriber_count;
        for (size_t index = 0u; index < count; index++) {
            subscribers[index] = s_subscribers[index];
        }
        xSemaphoreGive(s_subscriber_lock);
    }

    for (size_t index = 0u; index < count; index++) {
        subscribers[index].handler(event, subscribers[index].context);
    }
}

static void app_events_task(void *argument)
{
    app_event_t event;

    (void)argument;
    while (true) {
        if (xQueueReceive(s_queue, &event, portMAX_DELAY) == pdTRUE) {
            dispatch_event(&event);
        }
    }
}

esp_err_t app_events_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_subscriber_lock = xSemaphoreCreateMutex();
    if (s_subscriber_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_queue = xQueueCreate(APP_EVENTS_QUEUE_LENGTH, sizeof(app_event_t));
    if (s_queue == NULL) {
        vSemaphoreDelete(s_subscriber_lock);
        s_subscriber_lock = NULL;
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(app_events_task, "app_events", APP_EVENTS_TASK_STACK,
                    NULL, APP_EVENTS_TASK_PRIORITY, &s_task) != pdPASS) {
        vQueueDelete(s_queue);
        vSemaphoreDelete(s_subscriber_lock);
        s_queue = NULL;
        s_subscriber_lock = NULL;
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "event queue ready");
    return ESP_OK;
}

esp_err_t app_events_subscribe(app_event_handler_t handler, void *context)
{
    esp_err_t err = ESP_OK;

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (handler == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_subscriber_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_subscriber_count >= APP_EVENTS_MAX_SUBSCRIBERS) {
        err = ESP_ERR_NO_MEM;
    } else {
        s_subscribers[s_subscriber_count].handler = handler;
        s_subscribers[s_subscriber_count].context = context;
        s_subscriber_count++;
    }
    xSemaphoreGive(s_subscriber_lock);
    return err;
}

esp_err_t app_events_publish(app_event_type_t type,
                             app_event_source_t source,
                             app_event_severity_t severity,
                             int32_t value, int32_t detail, int32_t cause)
{
    app_event_t event = {
        .type = type,
        .source = source,
        .severity = severity,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000),
        .value = value,
        .detail = detail,
        .cause = cause,
    };

    if (!s_initialized || s_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    taskENTER_CRITICAL(&s_sequence_lock);
    event.sequence = s_next_sequence++;
    taskEXIT_CRITICAL(&s_sequence_lock);

    return xQueueSend(s_queue, &event, 0u) == pdTRUE
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
}
