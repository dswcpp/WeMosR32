#include "sht30_uart.h"

#include <stddef.h>

#include "driver/uart.h"
#include "esp_check.h"
#include "esp_timer.h"

#define SHT30_BAUD_RATE 9600
#define SHT30_UART_RX_BUFFER 512
#define SHT30_FRAME_MAX 40u
#define SHT30_HUMIDITY_MAX 1000
#define SHT30_TEMP_MIN (-400)
#define SHT30_TEMP_MAX 1250

typedef struct {
    char frame[SHT30_FRAME_MAX];
    uint8_t length;
    uint8_t overflow;
    uint32_t frames;
    uint32_t good;
    uint32_t bad;
} sht30_parser_t;

static sht30_parser_t s_parser;
static sht30_reading_t s_latest;
static int64_t s_latest_us;
static int s_uart_port;
static bool s_initialized;

static bool parse_digits(const char **cursor, const char *end, int max_digits,
                         int32_t *value)
{
    const char *p = *cursor;
    int32_t result = 0;
    int digits = 0;

    while (p < end && *p >= '0' && *p <= '9' && digits < max_digits) {
        result = result * 10 + (*p - '0');
        p++;
        digits++;
    }
    if (digits == 0) {
        return false;
    }

    *cursor = p;
    *value = result;
    return true;
}

static bool parse_frame(const char *frame, unsigned int length,
                        sht30_reading_t *out)
{
    const char *p = frame;
    const char *end = frame + length;
    int32_t humidity_int;
    int32_t humidity_frac;
    int32_t temperature_int;
    int32_t temperature_frac;
    int32_t humidity;
    int32_t temperature;
    bool negative = false;

    while (end > p && (end[-1] == '\r' || end[-1] == '\n')) {
        end--;
    }
    if (end - p < 2 || p[0] != 'R' || p[1] != ':') {
        return false;
    }
    p += 2;

    if (!parse_digits(&p, end, 3, &humidity_int) ||
        p >= end || *p != '.') {
        return false;
    }
    p++;
    if (!parse_digits(&p, end, 1, &humidity_frac) ||
        end - p < 2 || p[0] != 'R' || p[1] != 'H') {
        return false;
    }
    p += 2;

    if (p >= end || *p != ' ') {
        return false;
    }
    p++;

    if (p < end && *p == '-') {
        negative = true;
        p++;
    }
    if (!parse_digits(&p, end, 3, &temperature_int) ||
        p >= end || *p != '.') {
        return false;
    }
    p++;
    if (!parse_digits(&p, end, 1, &temperature_frac) ||
        p >= end || *p != 'C') {
        return false;
    }
    p++;
    if (p != end) {
        return false;
    }

    humidity = humidity_int * 10 + humidity_frac;
    temperature = temperature_int * 10 + temperature_frac;
    if (negative) {
        temperature = -temperature;
    }
    if (humidity > SHT30_HUMIDITY_MAX ||
        temperature < SHT30_TEMP_MIN ||
        temperature > SHT30_TEMP_MAX) {
        return false;
    }

    out->humidity_tenths_rh = (uint16_t)humidity;
    out->temperature_tenths_c = (int16_t)temperature;
    out->valid = true;
    return true;
}

static void store_reading(const sht30_reading_t *reading)
{
    s_latest = *reading;
    s_latest_us = esp_timer_get_time();
}

static void parser_feed(const uint8_t *data, unsigned int length)
{
    unsigned int i;

    for (i = 0; i < length; i++) {
        char c = (char)data[i];

        if (c == '\n') {
            unsigned int frame_length = s_parser.length;

            if (s_parser.overflow) {
                s_parser.overflow = 0u;
                s_parser.length = 0u;
                s_parser.frames++;
                s_parser.bad++;
                continue;
            }

            while (frame_length > 0u &&
                   s_parser.frame[frame_length - 1u] == '\r') {
                frame_length--;
            }
            s_parser.length = 0u;
            if (frame_length == 0u) {
                continue;
            }

            s_parser.frames++;
            {
                sht30_reading_t reading = {0};

                if (parse_frame(s_parser.frame, frame_length, &reading)) {
                    s_parser.good++;
                    store_reading(&reading);
                } else {
                    s_parser.bad++;
                }
            }
            continue;
        }

        if (s_parser.overflow) {
            continue;
        }
        if (s_parser.length >= SHT30_FRAME_MAX) {
            s_parser.overflow = 1u;
            s_parser.length = 0u;
            continue;
        }
        s_parser.frame[s_parser.length++] = c;
    }
}

esp_err_t sht30_uart_init(int uart_port, int tx_gpio, int rx_gpio)
{
    const uart_config_t config = {
        .baud_rate = SHT30_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };

    if (uart_port < 1 || uart_port > 2 ||
        tx_gpio < 0 || rx_gpio < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    s_parser = (sht30_parser_t){0};
    s_latest = (sht30_reading_t){0};
    s_latest_us = 0;
    s_uart_port = uart_port;

    ESP_RETURN_ON_ERROR(
        uart_driver_install((uart_port_t)uart_port, SHT30_UART_RX_BUFFER,
                            0, 0, NULL, 0),
        "sht30", "UART%d driver install failed", uart_port);
    ESP_RETURN_ON_ERROR(
        uart_param_config((uart_port_t)uart_port, &config),
        "sht30", "UART%d parameter configuration failed", uart_port);
    ESP_RETURN_ON_ERROR(
        uart_set_pin((uart_port_t)uart_port, tx_gpio, rx_gpio,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE),
        "sht30", "UART%d pin configuration failed", uart_port);
    ESP_RETURN_ON_ERROR(
        uart_flush_input((uart_port_t)uart_port),
        "sht30", "UART%d input flush failed", uart_port);

    s_initialized = true;
    return ESP_OK;
}

void sht30_uart_poll(void)
{
    uint8_t chunk[32];

    if (!s_initialized) {
        return;
    }

    for (;;) {
        int received = uart_read_bytes((uart_port_t)s_uart_port,
                                       chunk, sizeof(chunk), 0);
        if (received <= 0) {
            break;
        }
        parser_feed(chunk, (unsigned int)received);
    }
}

bool sht30_uart_get_latest(sht30_reading_t *reading, uint32_t *age_ms)
{
    int64_t now_us;
    uint32_t age;

    if (reading == NULL || !s_latest.valid) {
        return false;
    }

    now_us = esp_timer_get_time();
    age = now_us > s_latest_us
              ? (uint32_t)((now_us - s_latest_us) / 1000)
              : 0u;
    *reading = s_latest;
    if (age_ms != NULL) {
        *age_ms = age;
    }
    return true;
}

sht30_stats_t sht30_uart_get_stats(void)
{
    const sht30_stats_t stats = {
        .frames = s_parser.frames,
        .good = s_parser.good,
        .bad = s_parser.bad,
    };
    return stats;
}
