#include "stock_monitor_state.h"

#include <stddef.h>

static void clamp_selection(stock_monitor_state_t *state)
{
    if (state->symbol_count == 0u) {
        state->selected_index = 0u;
    } else if (state->selected_index >= state->symbol_count) {
        state->selected_index = (uint8_t)(state->symbol_count - 1u);
    }
}

void stock_monitor_state_init(stock_monitor_state_t *state,
                              uint8_t symbol_count)
{
    if (state == NULL) {
        return;
    }
    *state = (stock_monitor_state_t){
        .selected_index = 0u,
        .symbol_count = symbol_count,
        .view = STOCK_MONITOR_VIEW_QUOTE,
        .data_state = symbol_count == 0u ? STOCK_MONITOR_DATA_EMPTY
                                         : STOCK_MONITOR_DATA_WAITING,
    };
}

void stock_monitor_state_set_symbol_count(stock_monitor_state_t *state,
                                          uint8_t symbol_count)
{
    if (state == NULL) {
        return;
    }
    state->symbol_count = symbol_count;
    clamp_selection(state);
    if (symbol_count == 0u) {
        state->data_state = STOCK_MONITOR_DATA_EMPTY;
    } else if (state->data_state == STOCK_MONITOR_DATA_EMPTY) {
        state->data_state = STOCK_MONITOR_DATA_WAITING;
    }
}

stock_monitor_output_t stock_monitor_state_apply(
    stock_monitor_state_t *state, stock_monitor_event_t event)
{
    if (state == NULL) {
        return STOCK_MONITOR_OUTPUT_NONE;
    }

    switch (event) {
    case STOCK_MONITOR_EVENT_PREVIOUS:
        if (state->symbol_count > 0u) {
            state->selected_index =
                state->selected_index == 0u
                    ? (uint8_t)(state->symbol_count - 1u)
                    : (uint8_t)(state->selected_index - 1u);
            return STOCK_MONITOR_OUTPUT_NAVIGATION;
        }
        break;
    case STOCK_MONITOR_EVENT_NEXT:
        if (state->symbol_count > 0u) {
            state->selected_index =
                (uint8_t)((state->selected_index + 1u) %
                          state->symbol_count);
            return STOCK_MONITOR_OUTPUT_NAVIGATION;
        }
        break;
    case STOCK_MONITOR_EVENT_FIRST:
        if (state->symbol_count > 0u && state->selected_index != 0u) {
            state->selected_index = 0u;
            return STOCK_MONITOR_OUTPUT_NAVIGATION;
        }
        break;
    case STOCK_MONITOR_EVENT_LAST:
        if (state->symbol_count > 0u &&
            state->selected_index != state->symbol_count - 1u) {
            state->selected_index = (uint8_t)(state->symbol_count - 1u);
            return STOCK_MONITOR_OUTPUT_NAVIGATION;
        }
        break;
    case STOCK_MONITOR_EVENT_CYCLE_VIEW:
        state->view = state->view == STOCK_MONITOR_VIEW_DECISION
                          ? STOCK_MONITOR_VIEW_QUOTE
                          : (stock_monitor_view_t)(state->view + 1);
        return STOCK_MONITOR_OUTPUT_NAVIGATION;
    case STOCK_MONITOR_EVENT_SHOW_QUOTE:
        if (state->view != STOCK_MONITOR_VIEW_QUOTE) {
            state->view = STOCK_MONITOR_VIEW_QUOTE;
            return STOCK_MONITOR_OUTPUT_NAVIGATION;
        }
        break;
    case STOCK_MONITOR_EVENT_REFRESH_REQUESTED:
        if (state->symbol_count > 0u) {
            return STOCK_MONITOR_OUTPUT_REFRESH_REQUESTED;
        }
        break;
    case STOCK_MONITOR_EVENT_DATA_WAITING:
        state->data_state = STOCK_MONITOR_DATA_WAITING;
        break;
    case STOCK_MONITOR_EVENT_DATA_FRESH:
        state->data_state = STOCK_MONITOR_DATA_FRESH;
        return STOCK_MONITOR_OUTPUT_DATA_FRESH;
    case STOCK_MONITOR_EVENT_DATA_STALE:
        state->data_state = STOCK_MONITOR_DATA_STALE;
        return STOCK_MONITOR_OUTPUT_DATA_STALE;
    case STOCK_MONITOR_EVENT_DATA_ERROR:
        state->data_state = STOCK_MONITOR_DATA_ERROR;
        return STOCK_MONITOR_OUTPUT_DATA_ERROR;
    case STOCK_MONITOR_EVENT_DATA_EMPTY:
        state->data_state = STOCK_MONITOR_DATA_EMPTY;
        return STOCK_MONITOR_OUTPUT_DATA_EMPTY;
    case STOCK_MONITOR_EVENT_NONE:
    default:
        break;
    }
    return STOCK_MONITOR_OUTPUT_NONE;
}
