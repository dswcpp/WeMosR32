#include "startup_service.h"

#include <stddef.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static void lock_context(void)
{
    portENTER_CRITICAL(&s_lock);
}

static void unlock_context(void)
{
    portEXIT_CRITICAL(&s_lock);
}
#else
static void lock_context(void)
{
}

static void unlock_context(void)
{
}
#endif

static startup_snapshot_t s_snapshot;

static bool service_id_valid(startup_service_id_t service)
{
    return service >= STARTUP_SERVICE_CONFIG &&
           service < STARTUP_SERVICE_COUNT;
}

static bool service_is_boot_critical(startup_service_id_t service)
{
    return service == STARTUP_SERVICE_CONFIG ||
           service == STARTUP_SERVICE_EVENTS;
}

static int service_state_priority(startup_service_state_t state)
{
    switch (state) {
    case STARTUP_SERVICE_PENDING:
        return 0;
    case STARTUP_SERVICE_DISABLED:
        return 1;
    case STARTUP_SERVICE_STARTING:
        return 2;
    case STARTUP_SERVICE_READY:
        return 3;
    case STARTUP_SERVICE_DEGRADED:
        return 4;
    case STARTUP_SERVICE_FAILED:
        return 5;
    default:
        return 0;
    }
}

static bool state_can_replace(startup_service_state_t current,
                              startup_service_state_t next)
{
    if (current == next) {
        return true;
    }
    return service_state_priority(next) >
           service_state_priority(current);
}

void startup_service_begin(uint32_t now_ms,
                           startup_reset_reason_t reset_reason)
{
    lock_context();
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.state = STARTUP_STATE_BOOTING;
    s_snapshot.stage = STARTUP_STAGE_CORE;
    s_snapshot.reset_reason = reset_reason;
    s_snapshot.started_ms = now_ms;
    for (size_t index = 0u; index < STARTUP_SERVICE_COUNT; index++) {
        s_snapshot.services[index].state = STARTUP_SERVICE_PENDING;
    }
    unlock_context();
}

void startup_service_set_stage(startup_stage_t stage)
{
    if (stage < STARTUP_STAGE_CORE || stage > STARTUP_STAGE_READY) {
        return;
    }

    lock_context();
    if (!s_snapshot.complete) {
        s_snapshot.stage = stage;
    }
    unlock_context();
}

void startup_service_mark_starting(startup_service_id_t service,
                                   uint32_t now_ms)
{
    if (!service_id_valid(service)) {
        return;
    }

    lock_context();
    startup_service_record_t *record = &s_snapshot.services[service];
    if (!s_snapshot.complete &&
        (record->state == STARTUP_SERVICE_PENDING ||
         record->state == STARTUP_SERVICE_DISABLED)) {
        record->state = STARTUP_SERVICE_STARTING;
        record->started_ms = now_ms;
        record->finished_ms = 0u;
        record->duration_ms = 0u;
        record->last_error = 0;
    }
    unlock_context();
}

bool startup_service_record(startup_service_id_t service,
                            startup_service_state_t state,
                            int32_t last_error, uint32_t now_ms)
{
    bool may_continue = true;

    if (!service_id_valid(service) ||
        state < STARTUP_SERVICE_PENDING ||
        state > STARTUP_SERVICE_DISABLED) {
        return false;
    }

    lock_context();
    startup_service_record_t *record = &s_snapshot.services[service];
    if (!s_snapshot.complete &&
        state_can_replace(record->state, state)) {
        if (record->state != state) {
            if (state == STARTUP_SERVICE_DISABLED) {
                record->started_ms = 0u;
            } else if (record->state == STARTUP_SERVICE_PENDING) {
                record->started_ms = now_ms;
            }

            record->state = state;
            record->finished_ms = now_ms;
            record->duration_ms =
                record->started_ms == 0u
                    ? 0u
                    : (uint32_t)(now_ms - record->started_ms);
        }
        if (last_error != 0 && record->last_error == 0) {
            record->last_error = last_error;
        }
    }

    if (service_is_boot_critical(service) &&
        record->state == STARTUP_SERVICE_FAILED) {
        may_continue = false;
    }
    unlock_context();
    return may_continue;
}

void startup_service_finish(uint32_t now_ms)
{
    lock_context();
    startup_snapshot_t *snapshot = &s_snapshot;
    bool failed = false;
    bool degraded = false;

    if (snapshot->complete) {
        unlock_context();
        return;
    }

    for (size_t index = 0u; index < STARTUP_SERVICE_COUNT; index++) {
        const startup_service_record_t *record = &snapshot->services[index];

        if (service_is_boot_critical((startup_service_id_t)index) &&
            record->state != STARTUP_SERVICE_READY) {
            failed = true;
        } else if (record->state != STARTUP_SERVICE_READY &&
                   record->state != STARTUP_SERVICE_DISABLED) {
            degraded = true;
        }
    }

    snapshot->complete = true;
    snapshot->finished_ms = now_ms;
    snapshot->duration_ms =
        (uint32_t)(now_ms - snapshot->started_ms);
    snapshot->blocked = failed;
    snapshot->degraded = !failed && degraded;
    if (failed) {
        snapshot->state = STARTUP_STATE_FAILED;
    } else if (degraded) {
        snapshot->state = STARTUP_STATE_DEGRADED;
        snapshot->stage = STARTUP_STAGE_READY;
    } else {
        snapshot->state = STARTUP_STATE_READY;
        snapshot->stage = STARTUP_STAGE_READY;
    }
    unlock_context();
}

void startup_service_get_snapshot(startup_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    lock_context();
    *snapshot = s_snapshot;
    unlock_context();
}

const char *startup_state_name(startup_state_t state)
{
    switch (state) {
    case STARTUP_STATE_BOOTING:
        return "booting";
    case STARTUP_STATE_READY:
        return "ready";
    case STARTUP_STATE_DEGRADED:
        return "degraded";
    case STARTUP_STATE_FAILED:
        return "failed";
    default:
        return "unknown";
    }
}

const char *startup_service_state_name(startup_service_state_t state)
{
    switch (state) {
    case STARTUP_SERVICE_PENDING:
        return "pending";
    case STARTUP_SERVICE_STARTING:
        return "starting";
    case STARTUP_SERVICE_READY:
        return "ready";
    case STARTUP_SERVICE_DEGRADED:
        return "degraded";
    case STARTUP_SERVICE_FAILED:
        return "failed";
    case STARTUP_SERVICE_DISABLED:
        return "disabled";
    default:
        return "unknown";
    }
}

const char *startup_service_stage_name(startup_stage_t stage)
{
    switch (stage) {
    case STARTUP_STAGE_CORE:
        return "core";
    case STARTUP_STAGE_NETWORK:
        return "network";
    case STARTUP_STAGE_TIME:
        return "time";
    case STARTUP_STAGE_SERVICES:
        return "services";
    case STARTUP_STAGE_HTTP:
        return "http";
    case STARTUP_STAGE_READY:
        return "ready";
    default:
        return "unknown";
    }
}

const char *startup_service_name(startup_service_id_t service)
{
    switch (service) {
    case STARTUP_SERVICE_CONFIG:
        return "config";
    case STARTUP_SERVICE_EVENTS:
        return "events";
    case STARTUP_SERVICE_WIFI:
        return "wifi";
    case STARTUP_SERVICE_TIME:
        return "time";
    case STARTUP_SERVICE_WEATHER:
        return "weather";
    case STARTUP_SERVICE_FEEDBACK:
        return "feedback";
    case STARTUP_SERVICE_WELLNESS:
        return "wellness";
    case STARTUP_SERVICE_WORKSTATION_LIGHT:
        return "workstation_light";
    case STARTUP_SERVICE_HTTP:
        return "http";
    default:
        return "unknown";
    }
}

const char *startup_reset_reason_name(startup_reset_reason_t reason)
{
    switch (reason) {
    case STARTUP_RESET_POWER_ON:
        return "power_on";
    case STARTUP_RESET_EXTERNAL:
        return "external";
    case STARTUP_RESET_SOFTWARE:
        return "software";
    case STARTUP_RESET_PANIC:
        return "panic";
    case STARTUP_RESET_INTERRUPT_WATCHDOG:
        return "interrupt_watchdog";
    case STARTUP_RESET_TASK_WATCHDOG:
        return "task_watchdog";
    case STARTUP_RESET_WATCHDOG:
        return "watchdog";
    case STARTUP_RESET_DEEP_SLEEP:
        return "deep_sleep";
    case STARTUP_RESET_BROWNOUT:
        return "brownout";
    case STARTUP_RESET_SDIO:
        return "sdio";
    case STARTUP_RESET_USB:
        return "usb";
    case STARTUP_RESET_JTAG:
        return "jtag";
    case STARTUP_RESET_EFUSE:
        return "efuse";
    case STARTUP_RESET_POWER_GLITCH:
        return "power_glitch";
    case STARTUP_RESET_CPU_LOCKUP:
        return "cpu_lockup";
    case STARTUP_RESET_UNKNOWN:
    default:
        return "unknown";
    }
}
