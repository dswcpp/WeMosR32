#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RGB_INDICATOR_SYSTEM_BOOTING = 0,
    RGB_INDICATOR_SYSTEM_READY,
    RGB_INDICATOR_SYSTEM_DEGRADED,
} rgb_indicator_system_state_t;

typedef enum {
    RGB_INDICATOR_NETWORK_UNKNOWN = 0,
    RGB_INDICATOR_NETWORK_LOCAL_AP,
    RGB_INDICATOR_NETWORK_DISCONNECTED,
    RGB_INDICATOR_NETWORK_CONNECTING,
    RGB_INDICATOR_NETWORK_CONNECTED,
} rgb_indicator_network_state_t;

typedef enum {
    RGB_INDICATOR_STOCK_UNKNOWN = 0,
    RGB_INDICATOR_STOCK_EMPTY,
    RGB_INDICATOR_STOCK_WAITING,
    RGB_INDICATOR_STOCK_FRESH,
    RGB_INDICATOR_STOCK_STALE,
    RGB_INDICATOR_STOCK_ERROR,
} rgb_indicator_stock_state_t;

typedef enum {
    RGB_INDICATOR_SCENE_OFF = 0,
    RGB_INDICATOR_SCENE_BOOT,
    RGB_INDICATOR_SCENE_SYSTEM_READY,
    RGB_INDICATOR_SCENE_SYSTEM_DEGRADED,
    RGB_INDICATOR_SCENE_WIFI_LOCAL_AP,
    RGB_INDICATOR_SCENE_WIFI_OFFLINE,
    RGB_INDICATOR_SCENE_WIFI_CONNECTING,
    RGB_INDICATOR_SCENE_DATA_EMPTY,
    RGB_INDICATOR_SCENE_DATA_WAITING,
    RGB_INDICATOR_SCENE_DATA_FRESH,
    RGB_INDICATOR_SCENE_DATA_STALE,
    RGB_INDICATOR_SCENE_DATA_ERROR,
    RGB_INDICATOR_SCENE_REFRESHING,
    RGB_INDICATOR_SCENE_EVENT_NAVIGATE,
    RGB_INDICATOR_SCENE_EVENT_CONFIG_SAVED,
    RGB_INDICATOR_SCENE_EVENT_CONFIG_SAVE_FAILED,
    RGB_INDICATOR_SCENE_EVENT_WIFI_CONNECTED,
    RGB_INDICATOR_SCENE_EVENT_WIFI_DISCONNECTED,
    RGB_INDICATOR_SCENE_EVENT_CLIENT_CONNECTED,
    RGB_INDICATOR_SCENE_EVENT_CLIENT_DISCONNECTED,
    RGB_INDICATOR_SCENE_EVENT_REFRESH_STARTED,
    RGB_INDICATOR_SCENE_EVENT_REFRESH_SUCCEEDED,
    RGB_INDICATOR_SCENE_EVENT_REFRESH_FAILED,
    RGB_INDICATOR_SCENE_EVENT_PRICE_ALERT_UP,
    RGB_INDICATOR_SCENE_EVENT_PRICE_ALERT_DOWN,
    RGB_INDICATOR_SCENE_EVENT_SYSTEM_DEGRADED,
    RGB_INDICATOR_SCENE_COUNT,
} rgb_indicator_scene_t;

typedef enum {
    RGB_INDICATOR_EVENT_NONE = 0,
    RGB_INDICATOR_EVENT_NAVIGATE,
    RGB_INDICATOR_EVENT_CONFIG_SAVED,
    RGB_INDICATOR_EVENT_CONFIG_SAVE_FAILED,
    RGB_INDICATOR_EVENT_WIFI_CONNECTED,
    RGB_INDICATOR_EVENT_WIFI_DISCONNECTED,
    RGB_INDICATOR_EVENT_CLIENT_CONNECTED,
    RGB_INDICATOR_EVENT_CLIENT_DISCONNECTED,
    RGB_INDICATOR_EVENT_REFRESH_STARTED,
    RGB_INDICATOR_EVENT_REFRESH_SUCCEEDED,
    RGB_INDICATOR_EVENT_REFRESH_FAILED,
    RGB_INDICATOR_EVENT_PRICE_ALERT_UP,
    RGB_INDICATOR_EVENT_PRICE_ALERT_DOWN,
    RGB_INDICATOR_EVENT_SYSTEM_DEGRADED,
    RGB_INDICATOR_EVENT_COUNT,
} rgb_indicator_event_t;

typedef struct {
    rgb_indicator_system_state_t system;
    rgb_indicator_network_state_t network;
    rgb_indicator_stock_state_t stock;
    bool refreshing;
    bool symbols_configured;
} rgb_indicator_context_t;

typedef struct {
    bool active;
    rgb_indicator_event_t event;
    uint32_t started_ms;
    uint32_t duration_ms;
    uint8_t priority;
    uint32_t sequence;
} rgb_indicator_event_state_t;

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t brightness;
    /* 0..100. The driver applies this as a second brightness scale. */
    uint8_t pulse_percent;
} rgb_indicator_render_t;

typedef struct {
    rgb_indicator_scene_t base_scene;
    rgb_indicator_scene_t scene;
    rgb_indicator_event_t active_event;
    bool event_active;
    uint32_t event_sequence;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t brightness;
    uint8_t pulse_percent;
} rgb_indicator_resolved_t;

rgb_indicator_scene_t rgb_indicator_base_scene(
    const rgb_indicator_context_t *context);
bool rgb_indicator_trigger(rgb_indicator_event_state_t *event_state,
                           rgb_indicator_event_t event, uint32_t now_ms);
bool rgb_indicator_event_active(rgb_indicator_event_state_t *event_state,
                                uint32_t now_ms);
rgb_indicator_resolved_t rgb_indicator_resolve(
    const rgb_indicator_context_t *context,
    rgb_indicator_event_state_t *event_state, uint32_t now_ms);
void rgb_indicator_render(rgb_indicator_scene_t scene, uint32_t now_ms,
                          rgb_indicator_render_t *render);
const char *rgb_indicator_scene_name(rgb_indicator_scene_t scene);
const char *rgb_indicator_event_name(rgb_indicator_event_t event);

#ifdef __cplusplus
}
#endif
