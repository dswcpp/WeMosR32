#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WEATHER_SERVICE_LOCATION_MAX_LEN 48
#define WEATHER_SERVICE_ERROR_MAX_LEN 80
#define WEATHER_SERVICE_OBSERVED_AT_MAX_LEN 19

typedef struct {
    bool enabled;
    char location[WEATHER_SERVICE_LOCATION_MAX_LEN + 1];
    double latitude;
    double longitude;
} weather_service_settings_t;

typedef struct {
    bool enabled;
    char location[WEATHER_SERVICE_LOCATION_MAX_LEN + 1];
    double latitude;
    double longitude;

    bool available;
    bool stale;
    bool refreshing;
    bool current_valid;
    bool daily_valid;

    char observed_at[WEATHER_SERVICE_OBSERVED_AT_MAX_LEN + 1];
    double temperature_c;
    double apparent_temperature_c;
    int humidity_percent;
    int weather_code;
    bool is_day;

    int daily_weather_code;
    double temperature_max_c;
    double temperature_min_c;

    int64_t fetched_at_unix_s;
    int64_t fetched_age_s;
    int64_t last_attempt_age_s;
    uint32_t refresh_count;
    char last_error[WEATHER_SERVICE_ERROR_MAX_LEN + 1];
} weather_service_snapshot_t;

esp_err_t weather_service_init(void);
bool weather_service_is_initialized(void);

void weather_service_get_snapshot(weather_service_snapshot_t *snapshot);
esp_err_t weather_service_get_settings(weather_service_settings_t *settings);
esp_err_t weather_service_set_settings(
    const weather_service_settings_t *settings);
esp_err_t weather_service_request_refresh(void);

#ifdef __cplusplus
}
#endif
