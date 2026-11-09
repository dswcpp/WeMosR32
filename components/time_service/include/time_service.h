#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

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

esp_err_t time_service_init(void);
esp_err_t time_service_set_unix_time(int64_t unix_time);
void time_service_get_snapshot(time_service_snapshot_t *snapshot);
esp_err_t time_service_format_iso8601(char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif
