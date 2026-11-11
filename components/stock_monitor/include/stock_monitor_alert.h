#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STOCK_MONITOR_ALERT_NONE = 0,
    STOCK_MONITOR_ALERT_UP,
    STOCK_MONITOR_ALERT_DOWN,
} stock_monitor_alert_direction_t;

typedef struct {
    bool up_armed;
    bool down_armed;
} stock_monitor_alert_arm_t;

void stock_monitor_alert_arm_reset(stock_monitor_alert_arm_t *arm);

stock_monitor_alert_direction_t stock_monitor_alert_evaluate(
    double price, uint32_t upper_price_units, uint32_t lower_price_units,
    stock_monitor_alert_arm_t *arm);

#ifdef __cplusplus
}
#endif
