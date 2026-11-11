#pragma once

#include "stock_monitor_state.h"
#include "stock_service.h"

#ifdef __cplusplus
extern "C" {
#endif

void stock_monitor_ui_render(const stock_service_snapshot_t *snapshot,
                             const stock_monitor_state_t *state);

#ifdef __cplusplus
}
#endif
