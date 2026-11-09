#include <assert.h>
#include <stdio.h>

#include "pir_service.h"

typedef struct {
    bool active;
} fake_pir_t;

static bool sample_pir(void *context)
{
    return ((const fake_pir_t *)context)->active;
}

static pir_service_t make_service(fake_pir_t *fake, uint32_t hold_ms,
                                  uint8_t debounce_samples)
{
    const pir_service_config_t config = {
        .presence_hold_ms = hold_ms,
        .debounce_samples = debounce_samples,
    };
    pir_service_t service;

    pir_service_init(&service, &config, sample_pir, fake);
    return service;
}

static void stable_motion_emits_one_edge(void)
{
    fake_pir_t fake = {.active = false};
    pir_service_t service = make_service(&fake, 120000u, 3u);
    pir_service_update_result_t result;

    pir_service_update(&service, 0u, &result);
    assert(result.event_count == 0u);
    assert(!result.snapshot.motion);
    assert(!result.snapshot.present);

    fake.active = true;
    pir_service_update(&service, 20u, &result);
    assert(result.event_count == 0u);
    assert(!result.snapshot.motion);

    pir_service_update(&service, 40u, &result);
    assert(result.event_count == 0u);
    assert(!result.snapshot.motion);

    pir_service_update(&service, 60u, &result);
    assert(result.event_count == 1u);
    assert(result.events[0].type == PIR_SERVICE_EVENT_MOTION_STARTED);
    assert(result.events[0].order == 0u);
    assert(result.snapshot.motion);
    assert(result.snapshot.present);
    assert(result.snapshot.motion_events == 1u);

    pir_service_update(&service, 80u, &result);
    assert(result.event_count == 0u);
    assert(result.snapshot.motion_events == 1u);
}

static void presence_survives_brief_gap_then_expires(void)
{
    fake_pir_t fake = {.active = true};
    pir_service_t service = make_service(&fake, 120000u, 1u);
    pir_service_update_result_t result;

    pir_service_update(&service, 100u, &result);
    assert(result.event_count == 1u);
    assert(result.snapshot.present);

    fake.active = false;
    pir_service_update(&service, 1000u, &result);
    assert(result.event_count == 0u);
    assert(result.snapshot.present);
    assert(result.snapshot.presence_remaining_ms == 119100u);

    pir_service_update(&service, 120099u, &result);
    assert(result.event_count == 0u);
    assert(result.snapshot.present);

    pir_service_update(&service, 120100u, &result);
    assert(result.event_count == 1u);
    assert(result.events[0].type == PIR_SERVICE_EVENT_PRESENCE_EXPIRED);
    assert(!result.snapshot.present);
    assert(result.snapshot.presence_expirations == 1u);
}

static void stable_motion_refreshes_presence_window(void)
{
    fake_pir_t fake = {.active = true};
    pir_service_t service = make_service(&fake, 120000u, 1u);
    pir_service_update_result_t result;

    pir_service_update(&service, 0u, &result);
    pir_service_update(&service, 60000u, &result);

    fake.active = false;
    pir_service_update(&service, 100000u, &result);
    pir_service_update(&service, 179999u, &result);
    assert(result.snapshot.present);

    pir_service_update(&service, 180000u, &result);
    assert(!result.snapshot.present);
    assert(result.snapshot.presence_expirations == 1u);
}

static void elapsed_time_handles_tick_wraparound(void)
{
    fake_pir_t fake = {.active = true};
    pir_service_t service = make_service(&fake, 1024u, 1u);
    pir_service_update_result_t result;

    pir_service_update(&service, 0xFFFFFF00u, &result);
    fake.active = false;
    pir_service_update(&service, 0x00000100u, &result);
    assert(result.snapshot.present);

    pir_service_update(&service, 0x00000300u, &result);
    assert(!result.snapshot.present);
    assert(result.event_count == 1u);
}

int main(void)
{
    stable_motion_emits_one_edge();
    presence_survives_brief_gap_then_expires();
    stable_motion_refreshes_presence_window();
    elapsed_time_handles_tick_wraparound();
    puts("PIR service tests passed");
    return 0;
}
