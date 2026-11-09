#include "pir_service.h"

#include <stddef.h>

static uint32_t elapsed_ms(uint32_t now_ms, uint32_t since_ms)
{
    return now_ms - since_ms;
}

static uint32_t remaining_ms(uint32_t now_ms, uint32_t since_ms,
                             uint32_t duration_ms)
{
    uint32_t elapsed = elapsed_ms(now_ms, since_ms);

    return elapsed >= duration_ms ? 0u : duration_ms - elapsed;
}

static void emit_event(uint32_t now_ms, pir_service_update_result_t *result,
                       pir_service_event_type_t type)
{
    pir_service_event_record_t *event;

    if (result->event_count >= PIR_SERVICE_MAX_EVENTS_PER_UPDATE) {
        result->event_overflow = true;
        return;
    }

    event = &result->events[result->event_count];
    event->type = type;
    event->at_ms = now_ms;
    event->order = result->event_count;
    result->event_count++;
}

void pir_service_init(pir_service_t *service,
                      const pir_service_config_t *config,
                      pir_service_sample_fn_t sample, void *sample_context)
{
    if (service == NULL) {
        return;
    }

    *service = (pir_service_t){0};
    if (config != NULL) {
        service->config = *config;
    }
    if (service->config.debounce_samples == 0u) {
        service->config.debounce_samples = 1u;
    }
    service->sample = sample;
    service->sample_context = sample_context;
    service->initialized = true;
}

void pir_service_update(pir_service_t *service, uint32_t now_ms,
                        pir_service_update_result_t *result)
{
    bool raw_active;
    bool stable_changed = false;
    uint8_t previous_stable;

    if (result == NULL) {
        return;
    }
    *result = (pir_service_update_result_t){0};
    if (service == NULL || !service->initialized || service->sample == NULL) {
        return;
    }

    raw_active = service->sample(service->sample_context);
    previous_stable = service->stable;
    if ((uint8_t)raw_active == service->stable) {
        service->pending = 0u;
    } else {
        service->pending++;
        if (service->pending >= service->config.debounce_samples) {
            service->stable = raw_active ? 1u : 0u;
            service->pending = 0u;
            stable_changed = true;
        }
    }

    if (service->stable != 0u) {
        if (stable_changed && previous_stable == 0u) {
            service->motion_events++;
            emit_event(now_ms, result, PIR_SERVICE_EVENT_MOTION_STARTED);
        }
        service->has_motion = true;
        service->present = true;
        service->last_motion_ms = now_ms;
    } else if (service->present &&
               elapsed_ms(now_ms, service->last_motion_ms) >=
                   service->config.presence_hold_ms) {
        service->present = false;
        service->presence_expirations++;
        emit_event(now_ms, result, PIR_SERVICE_EVENT_PRESENCE_EXPIRED);
    }

    pir_service_get_snapshot(service, now_ms, &result->snapshot);
}

void pir_service_get_snapshot(const pir_service_t *service, uint32_t now_ms,
                              pir_service_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    *snapshot = (pir_service_snapshot_t){0};
    if (service == NULL || !service->initialized) {
        return;
    }

    snapshot->motion = service->stable != 0u;
    snapshot->present = service->present;
    snapshot->motion_events = service->motion_events;
    snapshot->presence_expirations = service->presence_expirations;
    if (service->has_motion) {
        snapshot->last_motion_age_ms =
            elapsed_ms(now_ms, service->last_motion_ms);
        snapshot->presence_remaining_ms =
            remaining_ms(now_ms, service->last_motion_ms,
                         service->config.presence_hold_ms);
    }
}
