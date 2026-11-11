#include "rgb_indicator_engine.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static rgb_indicator_context_t connected_context(void)
{
    return (rgb_indicator_context_t){
        .system = RGB_INDICATOR_SYSTEM_READY,
        .network = RGB_INDICATOR_NETWORK_CONNECTED,
        .stock = RGB_INDICATOR_STOCK_FRESH,
        .refreshing = false,
        .symbols_configured = true,
    };
}

static void base_state_priority_is_strict(void)
{
    rgb_indicator_context_t context = connected_context();

    assert(rgb_indicator_base_scene(&context) ==
           RGB_INDICATOR_SCENE_DATA_FRESH);

    context.refreshing = true;
    assert(rgb_indicator_base_scene(&context) ==
           RGB_INDICATOR_SCENE_REFRESHING);

    context.stock = RGB_INDICATOR_STOCK_STALE;
    assert(rgb_indicator_base_scene(&context) ==
           RGB_INDICATOR_SCENE_DATA_STALE);

    context.stock = RGB_INDICATOR_STOCK_ERROR;
    assert(rgb_indicator_base_scene(&context) ==
           RGB_INDICATOR_SCENE_DATA_ERROR);

    context.system = RGB_INDICATOR_SYSTEM_DEGRADED;
    assert(rgb_indicator_base_scene(&context) ==
           RGB_INDICATOR_SCENE_SYSTEM_DEGRADED);
    context.system = RGB_INDICATOR_SYSTEM_READY;

    context.network = RGB_INDICATOR_NETWORK_CONNECTING;
    assert(rgb_indicator_base_scene(&context) ==
           RGB_INDICATOR_SCENE_WIFI_CONNECTING);

    context.network = RGB_INDICATOR_NETWORK_DISCONNECTED;
    assert(rgb_indicator_base_scene(&context) ==
           RGB_INDICATOR_SCENE_WIFI_OFFLINE);

    context.system = RGB_INDICATOR_SYSTEM_BOOTING;
    assert(rgb_indicator_base_scene(&context) ==
           RGB_INDICATOR_SCENE_BOOT);
}

static void empty_and_local_ap_are_distinct(void)
{
    rgb_indicator_context_t context = connected_context();

    context.network = RGB_INDICATOR_NETWORK_LOCAL_AP;
    context.stock = RGB_INDICATOR_STOCK_EMPTY;
    assert(rgb_indicator_base_scene(&context) ==
           RGB_INDICATOR_SCENE_DATA_EMPTY);

    context.stock = RGB_INDICATOR_STOCK_UNKNOWN;
    assert(rgb_indicator_base_scene(&context) ==
           RGB_INDICATOR_SCENE_WIFI_LOCAL_AP);

    context.system = RGB_INDICATOR_SYSTEM_DEGRADED;
    assert(rgb_indicator_base_scene(&context) ==
           RGB_INDICATOR_SCENE_SYSTEM_DEGRADED);
}

static void events_obey_priority_and_expire(void)
{
    rgb_indicator_context_t context = connected_context();
    rgb_indicator_event_state_t events = {0};
    rgb_indicator_resolved_t resolved;

    assert(rgb_indicator_trigger(
        &events, RGB_INDICATOR_EVENT_REFRESH_FAILED, 1000u));
    resolved = rgb_indicator_resolve(&context, &events, 1100u);
    assert(resolved.scene == RGB_INDICATOR_SCENE_EVENT_REFRESH_FAILED);

    assert(!rgb_indicator_trigger(
        &events, RGB_INDICATOR_EVENT_NAVIGATE, 1200u));
    resolved = rgb_indicator_resolve(&context, &events, 1300u);
    assert(resolved.scene == RGB_INDICATOR_SCENE_EVENT_REFRESH_FAILED);

    assert(!rgb_indicator_trigger(
        &events, RGB_INDICATOR_EVENT_CONFIG_SAVE_FAILED, 1350u));
    resolved = rgb_indicator_resolve(&context, &events, 1360u);
    assert(resolved.scene == RGB_INDICATOR_SCENE_EVENT_REFRESH_FAILED);

    assert(rgb_indicator_trigger(
        &events, RGB_INDICATOR_EVENT_PRICE_ALERT_UP, 1400u));
    resolved = rgb_indicator_resolve(&context, &events, 1405u);
    assert(resolved.scene == RGB_INDICATOR_SCENE_EVENT_PRICE_ALERT_UP);
    assert(resolved.pulse_percent == 100u);

    resolved = rgb_indicator_resolve(&context, &events, 1550u);
    assert(resolved.pulse_percent == 0u);

    resolved = rgb_indicator_resolve(&context, &events, 3900u);
    assert(!resolved.event_active);
    assert(resolved.scene == RGB_INDICATOR_SCENE_DATA_FRESH);

    resolved = rgb_indicator_resolve(&context, &events, 3950u);
    assert(resolved.scene == RGB_INDICATOR_SCENE_DATA_FRESH);
}

static void stock_alert_colors_follow_china_market_convention(void)
{
    rgb_indicator_render_t up;
    rgb_indicator_render_t down;

    rgb_indicator_render(RGB_INDICATOR_SCENE_EVENT_PRICE_ALERT_UP, 0u,
                         &up);
    rgb_indicator_render(RGB_INDICATOR_SCENE_EVENT_PRICE_ALERT_DOWN, 0u,
                         &down);

    assert(up.red == 255u && up.green == 0u);
    assert(down.red == 0u && down.green == 255u);
    assert(up.brightness == 60u);
    assert(down.brightness == 50u);
}

static void every_scene_has_a_name_and_valid_render(void)
{
    for (int scene = RGB_INDICATOR_SCENE_OFF;
         scene < RGB_INDICATOR_SCENE_COUNT; scene++) {
        rgb_indicator_render_t render;

        assert(strcmp(
                   rgb_indicator_scene_name((rgb_indicator_scene_t)scene),
                   "unknown") != 0);
        rgb_indicator_render((rgb_indicator_scene_t)scene, 123u, &render);
        assert(render.red <= 255u);
        assert(render.green <= 255u);
        assert(render.blue <= 255u);
        assert(render.brightness <= 100u);
        assert(render.pulse_percent <= 100u);
    }
}

static void every_event_has_a_distinct_scene(void)
{
    rgb_indicator_context_t context = connected_context();
    bool scene_seen[RGB_INDICATOR_SCENE_COUNT] = {false};

    for (int event = RGB_INDICATOR_EVENT_NONE + 1;
         event < RGB_INDICATOR_EVENT_COUNT; event++) {
        rgb_indicator_event_state_t events = {0};
        rgb_indicator_resolved_t resolved;

        assert(strcmp(
                   rgb_indicator_event_name((rgb_indicator_event_t)event),
                   "unknown") != 0);
        assert(rgb_indicator_trigger(
            &events, (rgb_indicator_event_t)event, 100u));
        resolved = rgb_indicator_resolve(&context, &events, 101u);
        assert(resolved.event_active);
        assert(resolved.active_event == (rgb_indicator_event_t)event);
        assert(resolved.scene > RGB_INDICATOR_SCENE_REFRESHING);
        assert(resolved.scene < RGB_INDICATOR_SCENE_COUNT);
        assert(!scene_seen[resolved.scene]);
        scene_seen[resolved.scene] = true;
    }
}

static void breathing_and_blink_patterns_are_bounded(void)
{
    rgb_indicator_render_t render;
    uint8_t minimum = 100u;
    uint8_t maximum = 0u;

    for (uint32_t now = 0u; now <= 3000u; now += 10u) {
        rgb_indicator_render(RGB_INDICATOR_SCENE_WIFI_OFFLINE, now,
                             &render);
        assert(render.pulse_percent <= 100u);
        if (render.pulse_percent < minimum) {
            minimum = render.pulse_percent;
        }
        if (render.pulse_percent > maximum) {
            maximum = render.pulse_percent;
        }
    }
    assert(minimum == 0u);
    assert(maximum == 100u);

    rgb_indicator_render(RGB_INDICATOR_SCENE_EVENT_PRICE_ALERT_UP, 0u,
                         &render);
    assert(render.pulse_percent == 100u);
    rgb_indicator_render(RGB_INDICATOR_SCENE_EVENT_PRICE_ALERT_UP, 120u,
                         &render);
    assert(render.pulse_percent == 0u);
}

int main(void)
{
    base_state_priority_is_strict();
    empty_and_local_ap_are_distinct();
    events_obey_priority_and_expire();
    stock_alert_colors_follow_china_market_convention();
    every_scene_has_a_name_and_valid_render();
    every_event_has_a_distinct_scene();
    breathing_and_blink_patterns_are_bounded();
    puts("rgb indicator engine tests passed");
    return 0;
}
