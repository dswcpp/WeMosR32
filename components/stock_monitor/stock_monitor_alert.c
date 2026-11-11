#include "stock_monitor_alert.h"

#include <math.h>

#include "stock_types.h"

void stock_monitor_alert_arm_reset(stock_monitor_alert_arm_t *arm)
{
    if (arm == NULL) {
        return;
    }
    arm->up_armed = true;
    arm->down_armed = true;
}

stock_monitor_alert_direction_t stock_monitor_alert_evaluate(
    double price, uint32_t upper_price_units, uint32_t lower_price_units,
    stock_monitor_alert_arm_t *arm)
{
    const double units_per_yuan =
        (double)STOCK_ALERT_PRICE_UNITS_PER_YUAN;
    const double hysteresis =
        (double)STOCK_ALERT_PRICE_HYSTERESIS_UNITS / units_per_yuan;
    double upper_price;
    double lower_price;

    if (arm == NULL) {
        return STOCK_MONITOR_ALERT_NONE;
    }
    if (!isfinite(price) || price <= 0.0) {
        stock_monitor_alert_arm_reset(arm);
        return STOCK_MONITOR_ALERT_NONE;
    }

    upper_price = (double)upper_price_units / units_per_yuan;
    lower_price = (double)lower_price_units / units_per_yuan;

    if (upper_price_units == 0u) {
        arm->up_armed = true;
    } else {
        if (arm->up_armed && price >= upper_price) {
            arm->up_armed = false;
            return STOCK_MONITOR_ALERT_UP;
        }
        if (price <= upper_price - hysteresis) {
            arm->up_armed = true;
        }
    }

    if (lower_price_units == 0u) {
        arm->down_armed = true;
    } else {
        if (arm->down_armed && price <= lower_price) {
            arm->down_armed = false;
            return STOCK_MONITOR_ALERT_DOWN;
        }
        if (price >= lower_price + hysteresis) {
            arm->down_armed = true;
        }
    }
    return STOCK_MONITOR_ALERT_NONE;
}
