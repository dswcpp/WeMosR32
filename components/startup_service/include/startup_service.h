#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STARTUP_RESET_UNKNOWN = 0,
    STARTUP_RESET_POWER_ON,
    STARTUP_RESET_EXTERNAL,
    STARTUP_RESET_SOFTWARE,
    STARTUP_RESET_PANIC,
    STARTUP_RESET_INTERRUPT_WATCHDOG,
    STARTUP_RESET_TASK_WATCHDOG,
    STARTUP_RESET_WATCHDOG,
    STARTUP_RESET_DEEP_SLEEP,
    STARTUP_RESET_BROWNOUT,
    STARTUP_RESET_SDIO,
    STARTUP_RESET_USB,
    STARTUP_RESET_JTAG,
    STARTUP_RESET_EFUSE,
    STARTUP_RESET_POWER_GLITCH,
    STARTUP_RESET_CPU_LOCKUP,
} startup_reset_reason_t;

typedef enum {
    STARTUP_STAGE_CORE = 0,
    STARTUP_STAGE_NETWORK,
    STARTUP_STAGE_TIME,
    STARTUP_STAGE_SERVICES,
    STARTUP_STAGE_HTTP,
    STARTUP_STAGE_READY,
} startup_stage_t;

typedef enum {
    STARTUP_SERVICE_CONFIG = 0,
    STARTUP_SERVICE_EVENTS,
    STARTUP_SERVICE_WIFI,
    STARTUP_SERVICE_TIME,
    STARTUP_SERVICE_WEATHER,
    STARTUP_SERVICE_FEEDBACK,
    STARTUP_SERVICE_WELLNESS,
    STARTUP_SERVICE_WORKSTATION_LIGHT,
    STARTUP_SERVICE_HTTP,
    STARTUP_SERVICE_COUNT,
} startup_service_id_t;

typedef enum {
    STARTUP_SERVICE_PENDING = 0,
    STARTUP_SERVICE_STARTING,
    STARTUP_SERVICE_READY,
    STARTUP_SERVICE_DEGRADED,
    STARTUP_SERVICE_FAILED,
    STARTUP_SERVICE_DISABLED,
} startup_service_state_t;

typedef enum {
    STARTUP_STATE_BOOTING = 0,
    STARTUP_STATE_READY,
    STARTUP_STATE_DEGRADED,
    STARTUP_STATE_FAILED,
} startup_state_t;

typedef struct {
    startup_service_state_t state;
    int32_t last_error;
    uint32_t started_ms;
    uint32_t finished_ms;
    uint32_t duration_ms;
} startup_service_record_t;

typedef struct {
    startup_state_t state;
    startup_stage_t stage;
    startup_reset_reason_t reset_reason;
    bool complete;
    bool degraded;
    bool blocked;
    uint32_t started_ms;
    uint32_t finished_ms;
    uint32_t duration_ms;
    startup_service_record_t services[STARTUP_SERVICE_COUNT];
} startup_snapshot_t;

void startup_service_begin(uint32_t now_ms,
                           startup_reset_reason_t reset_reason);
void startup_service_set_stage(startup_stage_t stage);
void startup_service_mark_starting(startup_service_id_t service,
                                   uint32_t now_ms);

/*
 * Records the final state of one service.
 *
 * Failures are sticky: a later lower-severity result cannot hide an earlier
 * failed or degraded startup result. Returns false only when a boot-critical
 * service failed and startup cannot continue.
 */
bool startup_service_record(startup_service_id_t service,
                            startup_service_state_t state,
                            int32_t last_error, uint32_t now_ms);
void startup_service_finish(uint32_t now_ms);
void startup_service_get_snapshot(startup_snapshot_t *snapshot);

const char *startup_state_name(startup_state_t state);
const char *startup_service_state_name(startup_service_state_t state);
const char *startup_service_stage_name(startup_stage_t stage);
const char *startup_service_name(startup_service_id_t service);
const char *startup_reset_reason_name(startup_reset_reason_t reason);

#ifdef __cplusplus
}
#endif
