#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "stock_analyzer.h"
#include "stock_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool supported;
    bool valid;
    bool has_closed;
    stock_analyzer_result_t live;
    stock_analyzer_result_t closed;
} stock_analysis_snapshot_t;

typedef struct {
    bool ready;
    bool network_connected;
    bool refreshing;
    bool any_valid;
    uint8_t symbol_count;
    uint16_t refresh_seconds;
    uint32_t refresh_count;
    uint32_t failure_count;
    uint32_t seconds_since_update;
    uint8_t analysis_ready_count;
    char last_error[STOCK_ERROR_MAX_LEN];
    stock_quote_t quotes[STOCK_SERVICE_MAX_SYMBOLS];
    stock_price_history_t intraday_history[STOCK_SERVICE_MAX_SYMBOLS];
    stock_analysis_snapshot_t analysis[STOCK_SERVICE_MAX_SYMBOLS];
} stock_service_snapshot_t;

esp_err_t stock_service_start(void);
esp_err_t stock_service_set_config(const stock_service_config_t *config);
void stock_service_get_config(stock_service_config_t *config);
void stock_service_get_snapshot(stock_service_snapshot_t *snapshot);
esp_err_t stock_service_refresh_now(void);
bool stock_service_is_started(void);

#ifdef __cplusplus
}
#endif
