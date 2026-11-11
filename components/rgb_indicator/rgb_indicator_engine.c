#include "rgb_indicator_engine.h"

#include <stddef.h>

typedef enum {
    RGB_WAVE_SOLID = 0,
    RGB_WAVE_BREATHE,
    RGB_WAVE_BLINK,
    RGB_WAVE_DOUBLE_BLINK,
    RGB_WAVE_TRIPLE_BLINK,
    RGB_WAVE_PULSE,
} rgb_wave_t;

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t brightness;
    uint8_t minimum_percent;
    uint16_t period_ms;
    uint16_t on_ms;
    uint16_t off_ms;
    uint16_t duration_ms;
    uint8_t priority;
    rgb_wave_t wave;
} rgb_scene_profile_t;

static const rgb_scene_profile_t s_profiles[RGB_INDICATOR_SCENE_COUNT] = {
    [RGB_INDICATOR_SCENE_OFF] = {0},
    [RGB_INDICATOR_SCENE_BOOT] = {
        .red = 0u,
        .green = 64u,
        .blue = 255u,
        .brightness = 24u,
        .minimum_percent = 5u,
        .period_ms = 1200u,
        .wave = RGB_WAVE_BREATHE,
    },
    [RGB_INDICATOR_SCENE_SYSTEM_READY] = {
        .red = 0u,
        .green = 96u,
        .blue = 255u,
        .brightness = 6u,
        .wave = RGB_WAVE_SOLID,
    },
    [RGB_INDICATOR_SCENE_SYSTEM_DEGRADED] = {
        .red = 255u,
        .green = 110u,
        .blue = 0u,
        .brightness = 26u,
        .minimum_percent = 12u,
        .period_ms = 1800u,
        .wave = RGB_WAVE_BREATHE,
    },
    [RGB_INDICATOR_SCENE_WIFI_LOCAL_AP] = {
        .red = 0u,
        .green = 120u,
        .blue = 255u,
        .brightness = 12u,
        .minimum_percent = 10u,
        .period_ms = 3000u,
        .wave = RGB_WAVE_BREATHE,
    },
    [RGB_INDICATOR_SCENE_WIFI_OFFLINE] = {
        .red = 255u,
        .green = 96u,
        .blue = 0u,
        .brightness = 18u,
        .minimum_percent = 0u,
        .period_ms = 3000u,
        .wave = RGB_WAVE_BREATHE,
    },
    [RGB_INDICATOR_SCENE_WIFI_CONNECTING] = {
        .red = 0u,
        .green = 96u,
        .blue = 255u,
        .brightness = 28u,
        .minimum_percent = 12u,
        .period_ms = 1000u,
        .wave = RGB_WAVE_BREATHE,
    },
    [RGB_INDICATOR_SCENE_DATA_EMPTY] = {
        .red = 160u,
        .green = 0u,
        .blue = 255u,
        .brightness = 20u,
        .minimum_percent = 8u,
        .period_ms = 2400u,
        .wave = RGB_WAVE_BREATHE,
    },
    [RGB_INDICATOR_SCENE_DATA_WAITING] = {
        .red = 0u,
        .green = 160u,
        .blue = 255u,
        .brightness = 22u,
        .minimum_percent = 8u,
        .period_ms = 1400u,
        .wave = RGB_WAVE_BREATHE,
    },
    [RGB_INDICATOR_SCENE_DATA_FRESH] = {
        .red = 0u,
        .green = 210u,
        .blue = 64u,
        .brightness = 8u,
        .wave = RGB_WAVE_SOLID,
    },
    [RGB_INDICATOR_SCENE_DATA_STALE] = {
        .red = 255u,
        .green = 140u,
        .blue = 0u,
        .brightness = 18u,
        .minimum_percent = 6u,
        .period_ms = 2400u,
        .wave = RGB_WAVE_BREATHE,
    },
    [RGB_INDICATOR_SCENE_DATA_ERROR] = {
        .red = 255u,
        .green = 0u,
        .blue = 0u,
        .brightness = 30u,
        .minimum_percent = 5u,
        .period_ms = 1200u,
        .wave = RGB_WAVE_BREATHE,
    },
    [RGB_INDICATOR_SCENE_REFRESHING] = {
        .red = 0u,
        .green = 190u,
        .blue = 255u,
        .brightness = 30u,
        .minimum_percent = 10u,
        .period_ms = 600u,
        .wave = RGB_WAVE_BREATHE,
    },
    [RGB_INDICATOR_SCENE_EVENT_NAVIGATE] = {
        .red = 255u,
        .green = 255u,
        .blue = 255u,
        .brightness = 18u,
        .period_ms = 180u,
        .on_ms = 80u,
        .duration_ms = 160u,
        .priority = 10u,
        .wave = RGB_WAVE_PULSE,
    },
    [RGB_INDICATOR_SCENE_EVENT_CONFIG_SAVED] = {
        .red = 120u,
        .green = 200u,
        .blue = 255u,
        .brightness = 28u,
        .period_ms = 600u,
        .on_ms = 80u,
        .off_ms = 90u,
        .duration_ms = 700u,
        .priority = 40u,
        .wave = RGB_WAVE_DOUBLE_BLINK,
    },
    [RGB_INDICATOR_SCENE_EVENT_CONFIG_SAVE_FAILED] = {
        .red = 255u,
        .green = 40u,
        .blue = 0u,
        .brightness = 50u,
        .period_ms = 650u,
        .on_ms = 80u,
        .off_ms = 70u,
        .duration_ms = 900u,
        .priority = 75u,
        .wave = RGB_WAVE_TRIPLE_BLINK,
    },
    [RGB_INDICATOR_SCENE_EVENT_WIFI_CONNECTED] = {
        .red = 0u,
        .green = 255u,
        .blue = 180u,
        .brightness = 40u,
        .period_ms = 700u,
        .on_ms = 90u,
        .off_ms = 100u,
        .duration_ms = 800u,
        .priority = 50u,
        .wave = RGB_WAVE_DOUBLE_BLINK,
    },
    [RGB_INDICATOR_SCENE_EVENT_WIFI_DISCONNECTED] = {
        .red = 255u,
        .green = 64u,
        .blue = 0u,
        .brightness = 45u,
        .period_ms = 500u,
        .on_ms = 70u,
        .off_ms = 80u,
        .duration_ms = 1100u,
        .priority = 70u,
        .wave = RGB_WAVE_DOUBLE_BLINK,
    },
    [RGB_INDICATOR_SCENE_EVENT_CLIENT_CONNECTED] = {
        .red = 180u,
        .green = 220u,
        .blue = 255u,
        .brightness = 20u,
        .period_ms = 600u,
        .on_ms = 100u,
        .duration_ms = 650u,
        .priority = 20u,
        .wave = RGB_WAVE_PULSE,
    },
    [RGB_INDICATOR_SCENE_EVENT_CLIENT_DISCONNECTED] = {
        .red = 120u,
        .green = 160u,
        .blue = 255u,
        .brightness = 10u,
        .period_ms = 360u,
        .on_ms = 180u,
        .duration_ms = 360u,
        .priority = 15u,
        .wave = RGB_WAVE_PULSE,
    },
    [RGB_INDICATOR_SCENE_EVENT_REFRESH_STARTED] = {
        .red = 0u,
        .green = 190u,
        .blue = 255u,
        .brightness = 34u,
        .period_ms = 300u,
        .on_ms = 70u,
        .off_ms = 60u,
        .duration_ms = 1200u,
        .priority = 55u,
        .wave = RGB_WAVE_DOUBLE_BLINK,
    },
    [RGB_INDICATOR_SCENE_EVENT_REFRESH_SUCCEEDED] = {
        .red = 0u,
        .green = 255u,
        .blue = 80u,
        .brightness = 50u,
        .period_ms = 700u,
        .on_ms = 90u,
        .off_ms = 100u,
        .duration_ms = 800u,
        .priority = 60u,
        .wave = RGB_WAVE_DOUBLE_BLINK,
    },
    [RGB_INDICATOR_SCENE_EVENT_REFRESH_FAILED] = {
        .red = 255u,
        .green = 0u,
        .blue = 0u,
        .brightness = 55u,
        .period_ms = 900u,
        .on_ms = 80u,
        .off_ms = 90u,
        .duration_ms = 1100u,
        .priority = 80u,
        .wave = RGB_WAVE_TRIPLE_BLINK,
    },
    [RGB_INDICATOR_SCENE_EVENT_PRICE_ALERT_UP] = {
        .red = 255u,
        .green = 0u,
        .blue = 0u,
        .brightness = 60u,
        .period_ms = 240u,
        .duration_ms = 2400u,
        .priority = 90u,
        .wave = RGB_WAVE_BLINK,
    },
    [RGB_INDICATOR_SCENE_EVENT_PRICE_ALERT_DOWN] = {
        .red = 0u,
        .green = 255u,
        .blue = 80u,
        .brightness = 50u,
        .period_ms = 300u,
        .duration_ms = 2400u,
        .priority = 90u,
        .wave = RGB_WAVE_BLINK,
    },
    [RGB_INDICATOR_SCENE_EVENT_SYSTEM_DEGRADED] = {
        .red = 255u,
        .green = 110u,
        .blue = 0u,
        .brightness = 40u,
        .period_ms = 900u,
        .on_ms = 80u,
        .off_ms = 90u,
        .duration_ms = 1800u,
        .priority = 65u,
        .wave = RGB_WAVE_TRIPLE_BLINK,
    },
};

static rgb_indicator_scene_t scene_for_event(rgb_indicator_event_t event)
{
    switch (event) {
    case RGB_INDICATOR_EVENT_NAVIGATE:
        return RGB_INDICATOR_SCENE_EVENT_NAVIGATE;
    case RGB_INDICATOR_EVENT_CONFIG_SAVED:
        return RGB_INDICATOR_SCENE_EVENT_CONFIG_SAVED;
    case RGB_INDICATOR_EVENT_CONFIG_SAVE_FAILED:
        return RGB_INDICATOR_SCENE_EVENT_CONFIG_SAVE_FAILED;
    case RGB_INDICATOR_EVENT_WIFI_CONNECTED:
        return RGB_INDICATOR_SCENE_EVENT_WIFI_CONNECTED;
    case RGB_INDICATOR_EVENT_WIFI_DISCONNECTED:
        return RGB_INDICATOR_SCENE_EVENT_WIFI_DISCONNECTED;
    case RGB_INDICATOR_EVENT_CLIENT_CONNECTED:
        return RGB_INDICATOR_SCENE_EVENT_CLIENT_CONNECTED;
    case RGB_INDICATOR_EVENT_CLIENT_DISCONNECTED:
        return RGB_INDICATOR_SCENE_EVENT_CLIENT_DISCONNECTED;
    case RGB_INDICATOR_EVENT_REFRESH_STARTED:
        return RGB_INDICATOR_SCENE_EVENT_REFRESH_STARTED;
    case RGB_INDICATOR_EVENT_REFRESH_SUCCEEDED:
        return RGB_INDICATOR_SCENE_EVENT_REFRESH_SUCCEEDED;
    case RGB_INDICATOR_EVENT_REFRESH_FAILED:
        return RGB_INDICATOR_SCENE_EVENT_REFRESH_FAILED;
    case RGB_INDICATOR_EVENT_PRICE_ALERT_UP:
        return RGB_INDICATOR_SCENE_EVENT_PRICE_ALERT_UP;
    case RGB_INDICATOR_EVENT_PRICE_ALERT_DOWN:
        return RGB_INDICATOR_SCENE_EVENT_PRICE_ALERT_DOWN;
    case RGB_INDICATOR_EVENT_SYSTEM_DEGRADED:
        return RGB_INDICATOR_SCENE_EVENT_SYSTEM_DEGRADED;
    case RGB_INDICATOR_EVENT_NONE:
    default:
        return RGB_INDICATOR_SCENE_OFF;
    }
}

static bool elapsed_less(uint32_t now_ms, uint32_t started_ms,
                         uint32_t duration_ms)
{
    return duration_ms != 0u && (uint32_t)(now_ms - started_ms) < duration_ms;
}

static uint8_t triangle_percent(uint32_t phase, uint32_t period,
                                uint8_t minimum_percent)
{
    uint32_t wave;

    if (period == 0u) {
        return 100u;
    }
    wave = phase <= period / 2u
               ? phase * 2u
               : (period - phase) * 2u;
    wave = wave * 100u / period;
    return (uint8_t)(minimum_percent +
                     wave * (100u - minimum_percent) / 100u);
}

static bool within_pulse(uint32_t phase, uint16_t on_ms,
                         uint16_t off_ms, uint8_t pulse_index)
{
    uint32_t start = (uint32_t)pulse_index * (on_ms + off_ms);
    return phase >= start && phase < start + on_ms;
}

static uint8_t wave_percent(const rgb_scene_profile_t *profile,
                            uint32_t now_ms)
{
    uint32_t period = profile->period_ms;
    uint32_t phase;

    if (profile->wave == RGB_WAVE_SOLID || period == 0u) {
        return 100u;
    }
    phase = now_ms % period;

    switch (profile->wave) {
    case RGB_WAVE_BREATHE:
        return triangle_percent(phase, period,
                                profile->minimum_percent);
    case RGB_WAVE_BLINK:
        return phase < period / 2u ? 100u : 0u;
    case RGB_WAVE_DOUBLE_BLINK:
        return within_pulse(phase, profile->on_ms, profile->off_ms, 0u) ||
                       within_pulse(phase, profile->on_ms, profile->off_ms,
                                    1u)
                   ? 100u
                   : 0u;
    case RGB_WAVE_TRIPLE_BLINK:
        return within_pulse(phase, profile->on_ms, profile->off_ms, 0u) ||
                       within_pulse(phase, profile->on_ms, profile->off_ms,
                                    1u) ||
                       within_pulse(phase, profile->on_ms, profile->off_ms,
                                    2u)
                   ? 100u
                   : 0u;
    case RGB_WAVE_PULSE:
        return phase < profile->on_ms ? 100u : 0u;
    case RGB_WAVE_SOLID:
    default:
        return 100u;
    }
}

rgb_indicator_scene_t rgb_indicator_base_scene(
    const rgb_indicator_context_t *context)
{
    if (context == NULL ||
        context->system == RGB_INDICATOR_SYSTEM_BOOTING) {
        return RGB_INDICATOR_SCENE_BOOT;
    }

    if (context->network == RGB_INDICATOR_NETWORK_DISCONNECTED) {
        return RGB_INDICATOR_SCENE_WIFI_OFFLINE;
    }
    if (context->network == RGB_INDICATOR_NETWORK_CONNECTING) {
        return RGB_INDICATOR_SCENE_WIFI_CONNECTING;
    }
    if (context->system == RGB_INDICATOR_SYSTEM_DEGRADED) {
        return RGB_INDICATOR_SCENE_SYSTEM_DEGRADED;
    }

    switch (context->stock) {
    case RGB_INDICATOR_STOCK_ERROR:
        return RGB_INDICATOR_SCENE_DATA_ERROR;
    case RGB_INDICATOR_STOCK_STALE:
        return RGB_INDICATOR_SCENE_DATA_STALE;
    case RGB_INDICATOR_STOCK_EMPTY:
        return RGB_INDICATOR_SCENE_DATA_EMPTY;
    case RGB_INDICATOR_STOCK_WAITING:
        return context->refreshing ? RGB_INDICATOR_SCENE_REFRESHING
                                   : RGB_INDICATOR_SCENE_DATA_WAITING;
    case RGB_INDICATOR_STOCK_FRESH:
        return context->refreshing ? RGB_INDICATOR_SCENE_REFRESHING
                                   : RGB_INDICATOR_SCENE_DATA_FRESH;
    case RGB_INDICATOR_STOCK_UNKNOWN:
    default:
        break;
    }

    if (context->network == RGB_INDICATOR_NETWORK_LOCAL_AP) {
        return RGB_INDICATOR_SCENE_WIFI_LOCAL_AP;
    }
    return RGB_INDICATOR_SCENE_SYSTEM_READY;
}

bool rgb_indicator_event_active(rgb_indicator_event_state_t *event_state,
                                uint32_t now_ms)
{
    if (event_state == NULL || !event_state->active) {
        return false;
    }
    if (!elapsed_less(now_ms, event_state->started_ms,
                      event_state->duration_ms)) {
        event_state->active = false;
        event_state->event = RGB_INDICATOR_EVENT_NONE;
        return false;
    }
    return true;
}

bool rgb_indicator_trigger(rgb_indicator_event_state_t *event_state,
                           rgb_indicator_event_t event, uint32_t now_ms)
{
    rgb_indicator_scene_t scene;
    const rgb_scene_profile_t *profile;

    if (event_state == NULL || event <= RGB_INDICATOR_EVENT_NONE ||
        event >= RGB_INDICATOR_EVENT_COUNT) {
        return false;
    }

    scene = scene_for_event(event);
    profile = &s_profiles[scene];
    if (profile->duration_ms == 0u) {
        return false;
    }

    (void)rgb_indicator_event_active(event_state, now_ms);
    if (event_state->active &&
        profile->priority < event_state->priority) {
        return false;
    }

    event_state->active = true;
    event_state->event = event;
    event_state->started_ms = now_ms;
    event_state->duration_ms = profile->duration_ms;
    event_state->priority = profile->priority;
    event_state->sequence++;
    return true;
}

rgb_indicator_resolved_t rgb_indicator_resolve(
    const rgb_indicator_context_t *context,
    rgb_indicator_event_state_t *event_state, uint32_t now_ms)
{
    rgb_indicator_resolved_t resolved = {0};
    rgb_indicator_render_t render = {0};
    uint32_t render_time_ms = now_ms;

    resolved.base_scene = rgb_indicator_base_scene(context);
    resolved.scene = resolved.base_scene;
    if (rgb_indicator_event_active(event_state, now_ms)) {
        resolved.scene = scene_for_event(event_state->event);
        resolved.active_event = event_state->event;
        resolved.event_active = true;
        resolved.event_sequence = event_state->sequence;
        render_time_ms = (uint32_t)(now_ms - event_state->started_ms);
    }

    rgb_indicator_render(resolved.scene, render_time_ms, &render);
    resolved.red = render.red;
    resolved.green = render.green;
    resolved.blue = render.blue;
    resolved.brightness = render.brightness;
    resolved.pulse_percent = render.pulse_percent;
    return resolved;
}

void rgb_indicator_render(rgb_indicator_scene_t scene, uint32_t now_ms,
                          rgb_indicator_render_t *render)
{
    const rgb_scene_profile_t *profile;

    if (render == NULL) {
        return;
    }
    *render = (rgb_indicator_render_t){0};
    if (scene < RGB_INDICATOR_SCENE_OFF ||
        scene >= RGB_INDICATOR_SCENE_COUNT) {
        return;
    }

    profile = &s_profiles[scene];
    render->red = profile->red;
    render->green = profile->green;
    render->blue = profile->blue;
    render->brightness = profile->brightness;
    render->pulse_percent = wave_percent(profile, now_ms);
}

const char *rgb_indicator_scene_name(rgb_indicator_scene_t scene)
{
    switch (scene) {
    case RGB_INDICATOR_SCENE_OFF:
        return "off";
    case RGB_INDICATOR_SCENE_BOOT:
        return "boot";
    case RGB_INDICATOR_SCENE_SYSTEM_READY:
        return "system_ready";
    case RGB_INDICATOR_SCENE_SYSTEM_DEGRADED:
        return "system_degraded";
    case RGB_INDICATOR_SCENE_WIFI_LOCAL_AP:
        return "wifi_local_ap";
    case RGB_INDICATOR_SCENE_WIFI_OFFLINE:
        return "wifi_offline";
    case RGB_INDICATOR_SCENE_WIFI_CONNECTING:
        return "wifi_connecting";
    case RGB_INDICATOR_SCENE_DATA_EMPTY:
        return "data_empty";
    case RGB_INDICATOR_SCENE_DATA_WAITING:
        return "data_waiting";
    case RGB_INDICATOR_SCENE_DATA_FRESH:
        return "data_fresh";
    case RGB_INDICATOR_SCENE_DATA_STALE:
        return "data_stale";
    case RGB_INDICATOR_SCENE_DATA_ERROR:
        return "data_error";
    case RGB_INDICATOR_SCENE_REFRESHING:
        return "refreshing";
    case RGB_INDICATOR_SCENE_EVENT_NAVIGATE:
        return "event_navigate";
    case RGB_INDICATOR_SCENE_EVENT_CONFIG_SAVED:
        return "event_config_saved";
    case RGB_INDICATOR_SCENE_EVENT_CONFIG_SAVE_FAILED:
        return "event_config_save_failed";
    case RGB_INDICATOR_SCENE_EVENT_WIFI_CONNECTED:
        return "event_wifi_connected";
    case RGB_INDICATOR_SCENE_EVENT_WIFI_DISCONNECTED:
        return "event_wifi_disconnected";
    case RGB_INDICATOR_SCENE_EVENT_CLIENT_CONNECTED:
        return "event_client_connected";
    case RGB_INDICATOR_SCENE_EVENT_CLIENT_DISCONNECTED:
        return "event_client_disconnected";
    case RGB_INDICATOR_SCENE_EVENT_REFRESH_STARTED:
        return "event_refresh_started";
    case RGB_INDICATOR_SCENE_EVENT_REFRESH_SUCCEEDED:
        return "event_refresh_succeeded";
    case RGB_INDICATOR_SCENE_EVENT_REFRESH_FAILED:
        return "event_refresh_failed";
    case RGB_INDICATOR_SCENE_EVENT_PRICE_ALERT_UP:
        return "event_price_alert_up";
    case RGB_INDICATOR_SCENE_EVENT_PRICE_ALERT_DOWN:
        return "event_price_alert_down";
    case RGB_INDICATOR_SCENE_EVENT_SYSTEM_DEGRADED:
        return "event_system_degraded";
    case RGB_INDICATOR_SCENE_COUNT:
    default:
        return "unknown";
    }
}

const char *rgb_indicator_event_name(rgb_indicator_event_t event)
{
    switch (event) {
    case RGB_INDICATOR_EVENT_NONE:
        return "none";
    case RGB_INDICATOR_EVENT_NAVIGATE:
        return "navigate";
    case RGB_INDICATOR_EVENT_CONFIG_SAVED:
        return "config_saved";
    case RGB_INDICATOR_EVENT_CONFIG_SAVE_FAILED:
        return "config_save_failed";
    case RGB_INDICATOR_EVENT_WIFI_CONNECTED:
        return "wifi_connected";
    case RGB_INDICATOR_EVENT_WIFI_DISCONNECTED:
        return "wifi_disconnected";
    case RGB_INDICATOR_EVENT_CLIENT_CONNECTED:
        return "client_connected";
    case RGB_INDICATOR_EVENT_CLIENT_DISCONNECTED:
        return "client_disconnected";
    case RGB_INDICATOR_EVENT_REFRESH_STARTED:
        return "refresh_started";
    case RGB_INDICATOR_EVENT_REFRESH_SUCCEEDED:
        return "refresh_succeeded";
    case RGB_INDICATOR_EVENT_REFRESH_FAILED:
        return "refresh_failed";
    case RGB_INDICATOR_EVENT_PRICE_ALERT_UP:
        return "price_alert_up";
    case RGB_INDICATOR_EVENT_PRICE_ALERT_DOWN:
        return "price_alert_down";
    case RGB_INDICATOR_EVENT_SYSTEM_DEGRADED:
        return "system_degraded";
    case RGB_INDICATOR_EVENT_COUNT:
    default:
        return "unknown";
    }
}
