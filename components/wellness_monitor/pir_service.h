#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PIR_SERVICE_MAX_EVENTS_PER_UPDATE 2u

typedef bool (*pir_service_sample_fn_t)(void *context);

typedef struct {
    uint32_t presence_hold_ms;
    uint8_t debounce_samples;
} pir_service_config_t;

typedef enum {
    PIR_SERVICE_EVENT_NONE = 0,
    PIR_SERVICE_EVENT_MOTION_STARTED,
    PIR_SERVICE_EVENT_PRESENCE_EXPIRED,
} pir_service_event_type_t;

typedef struct {
    pir_service_event_type_t type;
    uint32_t at_ms;
    uint8_t order;
} pir_service_event_record_t;

typedef struct {
    bool motion;
    bool present;
    uint32_t motion_events;
    uint32_t presence_expirations;
    uint32_t last_motion_age_ms;
    uint32_t presence_remaining_ms;
} pir_service_snapshot_t;

typedef struct {
    pir_service_event_record_t events[PIR_SERVICE_MAX_EVENTS_PER_UPDATE];
    pir_service_snapshot_t snapshot;
    uint8_t event_count;
    bool event_overflow;
} pir_service_update_result_t;

typedef struct {
    pir_service_config_t config;
    pir_service_sample_fn_t sample;
    void *sample_context;
    uint32_t last_motion_ms;
    uint32_t motion_events;
    uint32_t presence_expirations;
    uint8_t stable;
    uint8_t pending;
    bool has_motion;
    bool present;
    bool initialized;
} pir_service_t;

void pir_service_init(pir_service_t *service,
                      const pir_service_config_t *config,
                      pir_service_sample_fn_t sample, void *sample_context);
void pir_service_update(pir_service_t *service, uint32_t now_ms,
                        pir_service_update_result_t *result);
void pir_service_get_snapshot(const pir_service_t *service, uint32_t now_ms,
                              pir_service_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif
