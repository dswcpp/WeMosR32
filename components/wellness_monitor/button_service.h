#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BUTTON_SERVICE_COUNT 4u

typedef enum {
    BUTTON_ID_ACK = 0,
    BUTTON_ID_WATER,
    BUTTON_ID_MUTE,
    BUTTON_ID_PAGE,
} button_id_t;

typedef enum {
    BUTTON_EVENT_NONE = 0,
    BUTTON_EVENT_SHORT_PRESS,
    BUTTON_EVENT_LONG_PRESS,
} button_event_type_t;

typedef struct {
    uint32_t debounce_ms;
    uint32_t long_press_ms;
} button_service_config_t;

typedef struct {
    button_event_type_t events[BUTTON_SERVICE_COUNT];
    uint8_t pressed_mask;
} button_service_update_result_t;

typedef struct {
    button_service_config_t config;
    uint8_t raw_mask;
    uint8_t stable_mask;
    uint32_t raw_changed_ms[BUTTON_SERVICE_COUNT];
    uint32_t pressed_since_ms[BUTTON_SERVICE_COUNT];
    bool initialized;
} button_service_t;

void button_service_init(button_service_t *service,
                         const button_service_config_t *config,
                         uint32_t now_ms, uint8_t initial_pressed_mask);
void button_service_update(button_service_t *service, uint32_t now_ms,
                           uint8_t raw_pressed_mask,
                           button_service_update_result_t *result);

#ifdef __cplusplus
}
#endif
