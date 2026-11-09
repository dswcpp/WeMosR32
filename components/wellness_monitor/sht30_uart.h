#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    int16_t temperature_tenths_c;
    uint16_t humidity_tenths_rh;
    bool valid;
} sht30_reading_t;

typedef struct {
    uint32_t frames;
    uint32_t good;
    uint32_t bad;
} sht30_stats_t;

esp_err_t sht30_uart_init(int uart_port, int tx_gpio, int rx_gpio);
void sht30_uart_poll(void);
bool sht30_uart_get_latest(sht30_reading_t *reading, uint32_t *age_ms);
sht30_stats_t sht30_uart_get_stats(void);

#ifdef __cplusplus
}
#endif
