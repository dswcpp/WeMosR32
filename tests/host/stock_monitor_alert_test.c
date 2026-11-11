#include "stock_monitor_alert.h"

#include <assert.h>
#include <math.h>

static void upper_and_lower_limits_trigger_independently(void)
{
    stock_monitor_alert_arm_t arm;

    stock_monitor_alert_arm_reset(&arm);
    assert(stock_monitor_alert_evaluate(9.99, 1000u, 500u, &arm) ==
           STOCK_MONITOR_ALERT_NONE);
    assert(stock_monitor_alert_evaluate(10.00, 1000u, 500u, &arm) ==
           STOCK_MONITOR_ALERT_UP);
    assert(stock_monitor_alert_evaluate(10.00, 1000u, 500u, &arm) ==
           STOCK_MONITOR_ALERT_NONE);
    assert(stock_monitor_alert_evaluate(9.98, 1000u, 500u, &arm) ==
           STOCK_MONITOR_ALERT_NONE);
    assert(stock_monitor_alert_evaluate(10.00, 1000u, 500u, &arm) ==
           STOCK_MONITOR_ALERT_UP);

    assert(stock_monitor_alert_evaluate(5.01, 1000u, 500u, &arm) ==
           STOCK_MONITOR_ALERT_NONE);
    assert(stock_monitor_alert_evaluate(5.00, 1000u, 500u, &arm) ==
           STOCK_MONITOR_ALERT_DOWN);
    assert(stock_monitor_alert_evaluate(5.00, 1000u, 500u, &arm) ==
           STOCK_MONITOR_ALERT_NONE);
    assert(stock_monitor_alert_evaluate(5.02, 1000u, 500u, &arm) ==
           STOCK_MONITOR_ALERT_NONE);
    assert(stock_monitor_alert_evaluate(5.00, 1000u, 500u, &arm) ==
           STOCK_MONITOR_ALERT_DOWN);
}

static void disabled_direction_does_not_block_the_other(void)
{
    stock_monitor_alert_arm_t arm;

    stock_monitor_alert_arm_reset(&arm);
    assert(stock_monitor_alert_evaluate(6.00, 0u, 500u, &arm) ==
           STOCK_MONITOR_ALERT_NONE);
    assert(stock_monitor_alert_evaluate(4.99, 0u, 500u, &arm) ==
           STOCK_MONITOR_ALERT_DOWN);
}

static void invalid_price_rearms_both_directions(void)
{
    stock_monitor_alert_arm_t arm = {
        .up_armed = false,
        .down_armed = false,
    };

    assert(stock_monitor_alert_evaluate(NAN, 1000u, 500u, &arm) ==
           STOCK_MONITOR_ALERT_NONE);
    assert(arm.up_armed);
    assert(arm.down_armed);
    assert(stock_monitor_alert_evaluate(0.0, 1000u, 500u, &arm) ==
           STOCK_MONITOR_ALERT_NONE);
    assert(arm.up_armed);
    assert(arm.down_armed);
}

int main(void)
{
    upper_and_lower_limits_trigger_independently();
    disabled_direction_does_not_block_the_other();
    invalid_price_rearms_both_directions();
    return 0;
}
