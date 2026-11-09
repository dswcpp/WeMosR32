#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "startup_service.h"

static void finish_all_optional_services(void)
{
    for (startup_service_id_t service = STARTUP_SERVICE_WIFI;
         service < STARTUP_SERVICE_COUNT; service++) {
        startup_service_mark_starting(service, 20u);
        assert(startup_service_record(service, STARTUP_SERVICE_READY, 0,
                                      25u));
    }
}

static void healthy_startup_reaches_ready(void)
{
    startup_snapshot_t snapshot;

    startup_service_begin(100u, STARTUP_RESET_POWER_ON);
    startup_service_mark_starting(STARTUP_SERVICE_CONFIG, 105u);
    assert(startup_service_record(STARTUP_SERVICE_CONFIG,
                                  STARTUP_SERVICE_READY, 0, 110u));
    startup_service_mark_starting(STARTUP_SERVICE_EVENTS, 111u);
    assert(startup_service_record(STARTUP_SERVICE_EVENTS,
                                  STARTUP_SERVICE_READY, 0, 118u));
    finish_all_optional_services();
    startup_service_set_stage(STARTUP_STAGE_READY);
    startup_service_finish(200u);

    startup_service_get_snapshot(&snapshot);
    assert(snapshot.complete);
    assert(!snapshot.degraded);
    assert(!snapshot.blocked);
    assert(snapshot.state == STARTUP_STATE_READY);
    assert(snapshot.stage == STARTUP_STAGE_READY);
    assert(snapshot.duration_ms == 100u);
    assert(snapshot.services[STARTUP_SERVICE_CONFIG].duration_ms == 5u);
    assert(snapshot.services[STARTUP_SERVICE_EVENTS].duration_ms == 7u);
    assert(strcmp(startup_state_name(snapshot.state), "ready") == 0);
}

static void optional_failure_degrades_startup(void)
{
    startup_snapshot_t snapshot;

    startup_service_begin(0u, STARTUP_RESET_BROWNOUT);
    startup_service_mark_starting(STARTUP_SERVICE_CONFIG, 0u);
    assert(startup_service_record(STARTUP_SERVICE_CONFIG,
                                  STARTUP_SERVICE_READY, 0, 1u));
    startup_service_mark_starting(STARTUP_SERVICE_EVENTS, 1u);
    assert(startup_service_record(STARTUP_SERVICE_EVENTS,
                                  STARTUP_SERVICE_READY, 0, 2u));

    startup_service_mark_starting(STARTUP_SERVICE_WIFI, 3u);
    assert(startup_service_record(STARTUP_SERVICE_WIFI,
                                  STARTUP_SERVICE_FAILED, 1234, 9u));
    for (startup_service_id_t service = STARTUP_SERVICE_TIME;
         service < STARTUP_SERVICE_COUNT; service++) {
        startup_service_mark_starting(service, 10u);
        assert(startup_service_record(service, STARTUP_SERVICE_READY, 0,
                                      12u));
    }
    startup_service_finish(50u);

    startup_service_get_snapshot(&snapshot);
    assert(snapshot.complete);
    assert(snapshot.degraded);
    assert(!snapshot.blocked);
    assert(snapshot.state == STARTUP_STATE_DEGRADED);
    assert(snapshot.stage == STARTUP_STAGE_READY);
    assert(snapshot.services[STARTUP_SERVICE_WIFI].last_error == 1234);
}

static void required_failure_blocks_and_failure_is_sticky(void)
{
    startup_snapshot_t snapshot;

    startup_service_begin(10u, STARTUP_RESET_TASK_WATCHDOG);
    startup_service_mark_starting(STARTUP_SERVICE_CONFIG, 11u);
    assert(!startup_service_record(STARTUP_SERVICE_CONFIG,
                                   STARTUP_SERVICE_FAILED, 0x101, 15u));
    assert(!startup_service_record(STARTUP_SERVICE_CONFIG,
                                   STARTUP_SERVICE_READY, 0, 20u));
    assert(!startup_service_record(STARTUP_SERVICE_CONFIG,
                                   STARTUP_SERVICE_FAILED, 0x202, 22u));
    startup_service_finish(30u);

    startup_service_get_snapshot(&snapshot);
    assert(snapshot.complete);
    assert(snapshot.blocked);
    assert(!snapshot.degraded);
    assert(snapshot.state == STARTUP_STATE_FAILED);
    assert(snapshot.stage == STARTUP_STAGE_CORE);
    assert(snapshot.services[STARTUP_SERVICE_CONFIG].state ==
           STARTUP_SERVICE_FAILED);
    assert(snapshot.services[STARTUP_SERVICE_CONFIG].last_error == 0x101);
}

static void disabled_services_are_valid_but_pending_services_degrade(void)
{
    startup_snapshot_t snapshot;

    startup_service_begin(0u, STARTUP_RESET_SOFTWARE);
    startup_service_mark_starting(STARTUP_SERVICE_CONFIG, 0u);
    assert(startup_service_record(STARTUP_SERVICE_CONFIG,
                                  STARTUP_SERVICE_READY, 0, 1u));
    startup_service_mark_starting(STARTUP_SERVICE_EVENTS, 1u);
    assert(startup_service_record(STARTUP_SERVICE_EVENTS,
                                  STARTUP_SERVICE_READY, 0, 2u));
    assert(startup_service_record(STARTUP_SERVICE_FEEDBACK,
                                  STARTUP_SERVICE_DISABLED, 0, 2u));
    assert(startup_service_record(STARTUP_SERVICE_WELLNESS,
                                  STARTUP_SERVICE_DISABLED, 0, 2u));
    startup_service_finish(10u);

    startup_service_get_snapshot(&snapshot);
    assert(snapshot.degraded);
    assert(snapshot.services[STARTUP_SERVICE_WIFI].state ==
           STARTUP_SERVICE_PENDING);
}

static void timestamps_remain_correct_across_wrap(void)
{
    startup_snapshot_t snapshot;

    startup_service_begin(UINT32_MAX - 5u, STARTUP_RESET_WATCHDOG);
    startup_service_mark_starting(STARTUP_SERVICE_CONFIG, UINT32_MAX - 5u);
    assert(startup_service_record(STARTUP_SERVICE_CONFIG,
                                  STARTUP_SERVICE_READY, 0, 4u));
    startup_service_finish(105u);

    startup_service_get_snapshot(&snapshot);
    assert(snapshot.duration_ms == 111u);
    assert(snapshot.services[STARTUP_SERVICE_CONFIG].duration_ms == 10u);
}

int main(void)
{
    healthy_startup_reaches_ready();
    optional_failure_degrades_startup();
    required_failure_blocks_and_failure_is_sticky();
    disabled_services_are_valid_but_pending_services_degrade();
    timestamps_remain_correct_across_wrap();
    puts("startup service tests passed");
    return 0;
}
