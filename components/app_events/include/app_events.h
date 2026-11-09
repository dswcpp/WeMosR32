#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_EVENTS_MAX_SUBSCRIBERS 8u

typedef enum {
    APP_EVENT_NONE = 0,
    APP_EVENT_SEDENTARY_GENTLE_NUDGE,
    APP_EVENT_SEDENTARY_ACTIVITY_NUDGE,
    APP_EVENT_SEDENTARY_ALERT_STARTED,
    APP_EVENT_SEDENTARY_ALERT_ESCALATED,
    APP_EVENT_SEDENTARY_SHORT_BREAK_RETURN,
    APP_EVENT_SEDENTARY_BREAK_COMPLETED,
    APP_EVENT_SEDENTARY_PHASE_CHANGED,
    APP_EVENT_CARE_WATER_REMINDER,
    APP_EVENT_WIFI_STATION_CONNECTED,
    APP_EVENT_WIFI_STATION_DISCONNECTED,
    APP_EVENT_WIFI_CLIENT_CONNECTED,
    APP_EVENT_WIFI_CLIENT_DISCONNECTED,
} app_event_type_t;

typedef enum {
    APP_EVENT_SOURCE_SYSTEM = 0,
    APP_EVENT_SOURCE_WELLNESS,
    APP_EVENT_SOURCE_CARE,
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
    /*
     * Event-specific payload. For APP_EVENT_SEDENTARY_PHASE_CHANGED,
     * value is the target phase, detail is the previous phase, and cause is
     * the sedentary_transition_reason_t value.
     */
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
