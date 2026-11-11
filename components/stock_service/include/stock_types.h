#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STOCK_SERVICE_MAX_SYMBOLS 8u
#define STOCK_SYMBOL_MAX_LEN 12u
#define STOCK_ERROR_MAX_LEN 64u
#define STOCK_UPDATED_AT_MAX_LEN 16u
#define STOCK_PRICE_HISTORY_MAX 64u
#define STOCK_REFRESH_MIN_SECONDS 15u
#define STOCK_REFRESH_MAX_SECONDS 3600u
#define STOCK_ALERT_PRICE_UNITS_PER_YUAN 100u
#define STOCK_ALERT_PRICE_MAX_UNITS 1000000u
#define STOCK_ALERT_PRICE_HYSTERESIS_UNITS 2u

typedef enum {
    STOCK_FEEDBACK_BOTH = 0,
    STOCK_FEEDBACK_VIBRATE,
    STOCK_FEEDBACK_SOUND,
    STOCK_FEEDBACK_SILENT,
} stock_feedback_mode_t;

typedef struct {
    bool alerts_enabled;
    uint8_t symbol_count;
    uint16_t refresh_seconds;
    uint32_t alert_upper_price_units[STOCK_SERVICE_MAX_SYMBOLS];
    uint32_t alert_lower_price_units[STOCK_SERVICE_MAX_SYMBOLS];
    stock_feedback_mode_t feedback_mode;
    char symbols[STOCK_SERVICE_MAX_SYMBOLS][STOCK_SYMBOL_MAX_LEN + 1u];
} stock_service_config_t;

typedef struct {
    uint8_t count;
    float prices[STOCK_PRICE_HISTORY_MAX];
} stock_price_history_t;

typedef struct {
    char symbol[STOCK_SYMBOL_MAX_LEN + 1u];
    char code[STOCK_SYMBOL_MAX_LEN + 1u];
    bool valid;
    double price;
    double previous_close;
    double open;
    double high;
    double low;
    double change;
    double change_percent;
    double volume_lots;
    double turnover_cny;
    char updated_at[STOCK_UPDATED_AT_MAX_LEN];
} stock_quote_t;

#ifdef __cplusplus
}
#endif
