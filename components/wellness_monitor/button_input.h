#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUTTON_INPUT_COUNT 4u

typedef struct {
    int gpios[BUTTON_INPUT_COUNT];
    bool initialized;
} button_input_t;

esp_err_t button_input_init(button_input_t *input,
                            const int gpios[BUTTON_INPUT_COUNT]);
uint8_t button_input_read_mask(const button_input_t *input);

#ifdef __cplusplus
}
#endif
