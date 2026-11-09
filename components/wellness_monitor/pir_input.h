#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int gpio;
    bool initialized;
} pir_input_t;

esp_err_t pir_input_init(pir_input_t *input, int gpio);
bool pir_input_read(const pir_input_t *input);

#ifdef __cplusplus
}
#endif
