#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUTTON_SERVICE_COUNT 4u

typedef enum {
    BUTTON_SERVICE_K1 = 0,
    BUTTON_SERVICE_K2,
    BUTTON_SERVICE_K3,
    BUTTON_SERVICE_K4,
} button_id_t;

typedef enum {
    BUTTON_EVENT_SHORT_PRESS = 0,
    BUTTON_EVENT_LONG_PRESS,
} button_event_type_t;

typedef struct {
    button_id_t button;
    button_event_type_t type;
    uint32_t held_ms;
} button_event_t;

typedef struct {
    int gpio[BUTTON_SERVICE_COUNT];
    bool active_low;
    uint32_t debounce_ms;
    uint32_t long_press_ms;
} button_service_config_t;

typedef struct {
    bool ready;
    bool pressed[BUTTON_SERVICE_COUNT];
    uint32_t short_presses[BUTTON_SERVICE_COUNT];
    uint32_t long_presses[BUTTON_SERVICE_COUNT];
} button_service_snapshot_t;

esp_err_t button_service_start(const button_service_config_t *config);
esp_err_t button_service_inject_event(button_id_t button,
                                      button_event_type_t type);
bool button_service_read_event(button_event_t *event, TickType_t wait_ticks);
bool button_service_get_snapshot(button_service_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif
