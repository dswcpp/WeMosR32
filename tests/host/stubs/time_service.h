#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    bool valid;
    bool synced;
    const char *source;
    const char *timezone;
    const char *ntp_server;
    const char *sync_endpoint;
    int64_t unix_time;
    int64_t utc_offset_s;
    int64_t last_sync_unix_s;
    int64_t last_sync_uptime_s;
    int64_t last_sync_age_s;
    uint32_t sync_count;
} time_service_snapshot_t;

static inline void time_service_get_snapshot(
    time_service_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->valid = true;
    snapshot->synced = true;
    snapshot->unix_time = 1794359100LL;
    snapshot->utc_offset_s = 8LL * 60LL * 60LL;
}
