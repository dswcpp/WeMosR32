#pragma once

#include <stdint.h>

#include "stock_service.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STOCK_DECISION_UNAVAILABLE = 0,
    STOCK_DECISION_AVOID,
    STOCK_DECISION_WAIT,
    STOCK_DECISION_READY,
} stock_decision_state_t;

typedef struct {
    stock_decision_state_t state;
    uint8_t confirmations;
    uint8_t confirmation_total;
    double entry_low;
    double entry_high;
    double invalidation;
    double target;
    double risk_percent;
    double reward_risk;
} stock_decision_t;

stock_analyzer_result_t stock_decision_effective_analysis(
    const stock_analysis_snapshot_t *analysis);

void stock_decision_evaluate(const stock_quote_t *quote,
                             const stock_analysis_snapshot_t *analysis,
                             stock_decision_t *decision);

#ifdef __cplusplus
}
#endif
