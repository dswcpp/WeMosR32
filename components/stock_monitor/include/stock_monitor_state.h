#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STOCK_MONITOR_VIEW_QUOTE = 0,
    STOCK_MONITOR_VIEW_INDICATORS,
    STOCK_MONITOR_VIEW_ANALYSIS,
    STOCK_MONITOR_VIEW_DECISION,
} stock_monitor_view_t;

typedef enum {
    STOCK_MONITOR_DATA_WAITING = 0,
    STOCK_MONITOR_DATA_FRESH,
    STOCK_MONITOR_DATA_STALE,
    STOCK_MONITOR_DATA_ERROR,
    STOCK_MONITOR_DATA_EMPTY,
} stock_monitor_data_state_t;

typedef enum {
    STOCK_MONITOR_EVENT_NONE = 0,
    STOCK_MONITOR_EVENT_PREVIOUS,
    STOCK_MONITOR_EVENT_NEXT,
    STOCK_MONITOR_EVENT_FIRST,
    STOCK_MONITOR_EVENT_LAST,
    STOCK_MONITOR_EVENT_CYCLE_VIEW,
    STOCK_MONITOR_EVENT_SHOW_QUOTE,
    STOCK_MONITOR_EVENT_REFRESH_REQUESTED,
    STOCK_MONITOR_EVENT_DATA_WAITING,
    STOCK_MONITOR_EVENT_DATA_FRESH,
    STOCK_MONITOR_EVENT_DATA_STALE,
    STOCK_MONITOR_EVENT_DATA_ERROR,
    STOCK_MONITOR_EVENT_DATA_EMPTY,
} stock_monitor_event_t;

typedef enum {
    STOCK_MONITOR_OUTPUT_NONE = 0,
    STOCK_MONITOR_OUTPUT_NAVIGATION,
    STOCK_MONITOR_OUTPUT_REFRESH_REQUESTED,
    STOCK_MONITOR_OUTPUT_DATA_FRESH,
    STOCK_MONITOR_OUTPUT_DATA_STALE,
    STOCK_MONITOR_OUTPUT_DATA_ERROR,
    STOCK_MONITOR_OUTPUT_DATA_EMPTY,
} stock_monitor_output_t;

typedef struct {
    uint8_t selected_index;
    uint8_t symbol_count;
    stock_monitor_view_t view;
    stock_monitor_data_state_t data_state;
} stock_monitor_state_t;

void stock_monitor_state_init(stock_monitor_state_t *state,
                              uint8_t symbol_count);
void stock_monitor_state_set_symbol_count(stock_monitor_state_t *state,
                                          uint8_t symbol_count);
stock_monitor_output_t stock_monitor_state_apply(
    stock_monitor_state_t *state, stock_monitor_event_t event);

#ifdef __cplusplus
}
#endif
