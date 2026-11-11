#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "status_led_pattern.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int gpio;
    bool active_high;
} status_led_config_t;

esp_err_t status_led_service_start(const status_led_config_t *config);
void status_led_service_set_mode(status_led_mode_t mode);

#ifdef __cplusplus
}
#endif
