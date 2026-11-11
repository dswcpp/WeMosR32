#include "stock_monitor_state.h"

#include <assert.h>
#include <stdio.h>

static void navigation_wraps_and_clamps(void)
{
    stock_monitor_state_t state;

    stock_monitor_state_init(&state, 3u);
    assert(state.selected_index == 0u);
    assert(stock_monitor_state_apply(
               &state, STOCK_MONITOR_EVENT_PREVIOUS) ==
           STOCK_MONITOR_OUTPUT_NAVIGATION);
    assert(state.selected_index == 2u);
    assert(stock_monitor_state_apply(
               &state, STOCK_MONITOR_EVENT_NEXT) ==
           STOCK_MONITOR_OUTPUT_NAVIGATION);
    assert(state.selected_index == 0u);
    assert(stock_monitor_state_apply(
               &state, STOCK_MONITOR_EVENT_LAST) ==
           STOCK_MONITOR_OUTPUT_NAVIGATION);
    assert(state.selected_index == 2u);
    assert(stock_monitor_state_apply(
               &state, STOCK_MONITOR_EVENT_FIRST) ==
           STOCK_MONITOR_OUTPUT_NAVIGATION);
    assert(state.selected_index == 0u);

    stock_monitor_state_set_symbol_count(&state, 1u);
    assert(state.selected_index == 0u);
    stock_monitor_state_set_symbol_count(&state, 0u);
    assert(state.data_state == STOCK_MONITOR_DATA_EMPTY);
}

static void views_cycle_and_return_to_quote(void)
{
    stock_monitor_state_t state;

    stock_monitor_state_init(&state, 1u);
    assert(state.view == STOCK_MONITOR_VIEW_QUOTE);
    assert(stock_monitor_state_apply(
               &state, STOCK_MONITOR_EVENT_CYCLE_VIEW) ==
           STOCK_MONITOR_OUTPUT_NAVIGATION);
    assert(state.view == STOCK_MONITOR_VIEW_INDICATORS);
    assert(stock_monitor_state_apply(
               &state, STOCK_MONITOR_EVENT_CYCLE_VIEW) ==
           STOCK_MONITOR_OUTPUT_NAVIGATION);
    assert(state.view == STOCK_MONITOR_VIEW_ANALYSIS);
    assert(stock_monitor_state_apply(
               &state, STOCK_MONITOR_EVENT_CYCLE_VIEW) ==
           STOCK_MONITOR_OUTPUT_NAVIGATION);
    assert(state.view == STOCK_MONITOR_VIEW_DECISION);
    assert(stock_monitor_state_apply(
               &state, STOCK_MONITOR_EVENT_CYCLE_VIEW) ==
           STOCK_MONITOR_OUTPUT_NAVIGATION);
    assert(state.view == STOCK_MONITOR_VIEW_QUOTE);

    (void)stock_monitor_state_apply(
        &state, STOCK_MONITOR_EVENT_CYCLE_VIEW);
    (void)stock_monitor_state_apply(
        &state, STOCK_MONITOR_EVENT_CYCLE_VIEW);
    assert(stock_monitor_state_apply(
               &state, STOCK_MONITOR_EVENT_SHOW_QUOTE) ==
           STOCK_MONITOR_OUTPUT_NAVIGATION);
    assert(state.view == STOCK_MONITOR_VIEW_QUOTE);
}

static void data_and_refresh_events(void)
{
    stock_monitor_state_t state;

    stock_monitor_state_init(&state, 2u);
    assert(state.data_state == STOCK_MONITOR_DATA_WAITING);
    assert(stock_monitor_state_apply(
               &state, STOCK_MONITOR_EVENT_DATA_FRESH) ==
           STOCK_MONITOR_OUTPUT_DATA_FRESH);
    assert(state.data_state == STOCK_MONITOR_DATA_FRESH);
    assert(stock_monitor_state_apply(
               &state, STOCK_MONITOR_EVENT_DATA_STALE) ==
           STOCK_MONITOR_OUTPUT_DATA_STALE);
    assert(state.data_state == STOCK_MONITOR_DATA_STALE);
    assert(stock_monitor_state_apply(
               &state, STOCK_MONITOR_EVENT_DATA_ERROR) ==
           STOCK_MONITOR_OUTPUT_DATA_ERROR);
    assert(state.data_state == STOCK_MONITOR_DATA_ERROR);
    assert(stock_monitor_state_apply(
               &state, STOCK_MONITOR_EVENT_REFRESH_REQUESTED) ==
           STOCK_MONITOR_OUTPUT_REFRESH_REQUESTED);

    stock_monitor_state_set_symbol_count(&state, 0u);
    assert(stock_monitor_state_apply(
               &state, STOCK_MONITOR_EVENT_REFRESH_REQUESTED) ==
           STOCK_MONITOR_OUTPUT_NONE);
}

int main(void)
{
    navigation_wraps_and_clamps();
    views_cycle_and_return_to_quote();
    data_and_refresh_events();
    puts("stock monitor state tests passed");
    return 0;
}
