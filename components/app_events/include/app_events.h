#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_EVENTS_MAX_SUBSCRIBERS 8u

typedef enum {
    APP_EVENT_NONE = 0,
    APP_EVENT_STOCK_REFRESHED,
    APP_EVENT_STOCK_REFRESH_FAILED,
    APP_EVENT_STOCK_REFRESH_STARTED,
    APP_EVENT_STOCK_PRICE_ALERT,
    APP_EVENT_STOCK_VIEW_CHANGED,
    APP_EVENT_WIFI_STATION_CONNECTING,
    APP_EVENT_WIFI_STATION_CONNECTED,
    APP_EVENT_WIFI_STATION_DISCONNECTED,
    APP_EVENT_WIFI_CLIENT_CONNECTED,
    APP_EVENT_WIFI_CLIENT_DISCONNECTED,
    APP_EVENT_CONFIG_UPDATED,
    APP_EVENT_CONFIG_SAVE_FAILED,
    APP_EVENT_SYSTEM_DEGRADED,
} app_event_type_t;

typedef enum {
    APP_EVENT_SOURCE_SYSTEM = 0,
    APP_EVENT_SOURCE_STOCK,
    APP_EVENT_SOURCE_MONITOR,
    APP_EVENT_SOURCE_WIFI,
} app_event_source_t;

typedef enum {
    APP_EVENT_SEVERITY_INFO = 0,
    APP_EVENT_SEVERITY_WARNING,
    APP_EVENT_SEVERITY_ALERT,
} app_event_severity_t;

typedef struct {
    app_event_type_t type;
    app_event_source_t source;
    app_event_severity_t severity;
    uint32_t timestamp_ms;
    uint32_t sequence;
    /* Event-specific payload. */
    int32_t value;
    int32_t detail;
    int32_t cause;
} app_event_t;

typedef void (*app_event_handler_t)(const app_event_t *event, void *context);

esp_err_t app_events_init(void);
esp_err_t app_events_subscribe(app_event_handler_t handler, void *context);
esp_err_t app_events_publish(app_event_type_t type,
                             app_event_source_t source,
                             app_event_severity_t severity,
                             int32_t value, int32_t detail, int32_t cause);

#ifdef __cplusplus
}
#endif
