#include "button_service.h"

#include <stddef.h>

static uint32_t elapsed_ms(uint32_t now_ms, uint32_t since_ms)
{
    return now_ms - since_ms;
}

void button_service_init(button_service_t *service,
                         const button_service_config_t *config,
                         uint32_t now_ms, uint8_t initial_pressed_mask)
{
    if (service == NULL) {
        return;
    }

    *service = (button_service_t){
        .raw_mask = (uint8_t)(initial_pressed_mask &
                              ((1u << BUTTON_SERVICE_COUNT) - 1u)),
        .stable_mask = (uint8_t)(initial_pressed_mask &
                                 ((1u << BUTTON_SERVICE_COUNT) - 1u)),
        .initialized = true,
    };
    if (config != NULL) {
        service->config = *config;
    }
    if (service->config.debounce_ms == 0u) {
        service->config.debounce_ms = 1u;
    }
    if (service->config.long_press_ms == 0u) {
        service->config.long_press_ms = service->config.debounce_ms;
    }

    for (uint8_t index = 0u; index < BUTTON_SERVICE_COUNT; index++) {
        service->raw_changed_ms[index] = now_ms;
        service->pressed_since_ms[index] = now_ms;
    }
}

void button_service_update(button_service_t *service, uint32_t now_ms,
                           uint8_t raw_pressed_mask,
                           button_service_update_result_t *result)
{
    if (result == NULL) {
        return;
    }

    *result = (button_service_update_result_t){0};
    if (service == NULL || !service->initialized) {
        return;
    }

    for (uint8_t index = 0u; index < BUTTON_SERVICE_COUNT; index++) {
        bool raw_pressed =
            (raw_pressed_mask & (uint8_t)(1u << index)) != 0u;
        bool raw_before =
            (service->raw_mask & (uint8_t)(1u << index)) != 0u;
        bool stable_pressed =
            (service->stable_mask & (uint8_t)(1u << index)) != 0u;

        if (raw_pressed != raw_before) {
            if (raw_pressed) {
                service->raw_mask |= (uint8_t)(1u << index);
            } else {
                service->raw_mask &= (uint8_t)~(1u << index);
            }
            service->raw_changed_ms[index] = now_ms;
        }

        if (stable_pressed == raw_pressed ||
            elapsed_ms(now_ms, service->raw_changed_ms[index]) <
                service->config.debounce_ms) {
            continue;
        }

        if (raw_pressed) {
            service->stable_mask |= (uint8_t)(1u << index);
            service->pressed_since_ms[index] = now_ms;
        } else {
            uint32_t duration =
                elapsed_ms(now_ms, service->pressed_since_ms[index]);

            service->stable_mask &= (uint8_t)~(1u << index);
            result->events[index] =
                duration >= service->config.long_press_ms
                    ? BUTTON_EVENT_LONG_PRESS
                    : BUTTON_EVENT_SHORT_PRESS;
        }
    }

    result->pressed_mask = service->stable_mask;
}
