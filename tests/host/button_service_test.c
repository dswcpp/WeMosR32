#include <assert.h>
#include <stdio.h>

#include "button_service.h"

static button_service_t make_service(void)
{
    const button_service_config_t config = {
        .debounce_ms = 25u,
        .long_press_ms = 1500u,
    };
    button_service_t service;

    button_service_init(&service, &config, 0u, 0u);
    return service;
}

static void bounce_shorter_than_debounce_is_ignored(void)
{
    button_service_t service = make_service();
    button_service_update_result_t result;

    button_service_update(&service, 0u, 0x01u, &result);
    assert(result.events[BUTTON_ID_ACK] == BUTTON_EVENT_NONE);
    button_service_update(&service, 10u, 0x00u, &result);
    assert(result.events[BUTTON_ID_ACK] == BUTTON_EVENT_NONE);
    button_service_update(&service, 40u, 0x00u, &result);
    assert(result.events[BUTTON_ID_ACK] == BUTTON_EVENT_NONE);
    assert(result.pressed_mask == 0u);
}

static void release_after_debounce_emits_short_press(void)
{
    button_service_t service = make_service();
    button_service_update_result_t result;

    button_service_update(&service, 0u, 0x01u, &result);
    assert(result.pressed_mask == 0u);
    button_service_update(&service, 25u, 0x01u, &result);
    assert(result.pressed_mask == 0x01u);
    assert(result.events[BUTTON_ID_ACK] == BUTTON_EVENT_NONE);

    button_service_update(&service, 100u, 0x00u, &result);
    assert(result.pressed_mask == 0x01u);
    assert(result.events[BUTTON_ID_ACK] == BUTTON_EVENT_NONE);

    button_service_update(&service, 125u, 0x00u, &result);
    assert(result.pressed_mask == 0u);
    assert(result.events[BUTTON_ID_ACK] == BUTTON_EVENT_SHORT_PRESS);

    button_service_update(&service, 200u, 0x00u, &result);
    assert(result.events[BUTTON_ID_ACK] == BUTTON_EVENT_NONE);
}

static void long_hold_emits_only_on_release(void)
{
    button_service_t service = make_service();
    button_service_update_result_t result;

    button_service_update(&service, 0u, 0x02u, &result);
    button_service_update(&service, 25u, 0x02u, &result);
    assert(result.pressed_mask == 0x02u);

    button_service_update(&service, 1499u, 0x02u, &result);
    assert(result.events[BUTTON_ID_WATER] == BUTTON_EVENT_NONE);

    button_service_update(&service, 1500u, 0x00u, &result);
    assert(result.events[BUTTON_ID_WATER] == BUTTON_EVENT_NONE);
    button_service_update(&service, 1525u, 0x00u, &result);
    assert(result.events[BUTTON_ID_WATER] == BUTTON_EVENT_LONG_PRESS);
}

static void simultaneous_keys_report_independent_events(void)
{
    button_service_t service = make_service();
    button_service_update_result_t result;

    button_service_update(&service, 0u, 0x09u, &result);
    button_service_update(&service, 25u, 0x09u, &result);
    assert(result.pressed_mask == 0x09u);
    button_service_update(&service, 100u, 0x00u, &result);
    button_service_update(&service, 125u, 0x00u, &result);

    assert(result.events[BUTTON_ID_ACK] == BUTTON_EVENT_SHORT_PRESS);
    assert(result.events[BUTTON_ID_PAGE] == BUTTON_EVENT_SHORT_PRESS);
    assert(result.events[BUTTON_ID_WATER] == BUTTON_EVENT_NONE);
    assert(result.events[BUTTON_ID_MUTE] == BUTTON_EVENT_NONE);
}

int main(void)
{
    bounce_shorter_than_debounce_is_ignored();
    release_after_debounce_emits_short_press();
    long_hold_emits_only_on_release();
    simultaneous_keys_report_independent_events();
    puts("button service tests passed");
    return 0;
}
