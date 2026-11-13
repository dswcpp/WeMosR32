#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define SSD1306_WIDTH 128
#define SSD1306_HEIGHT 64
#define SSD1306_PAGE_COUNT 8
#define SSD1306_FONT_ADVANCE 6

typedef struct {
    int i2c_port;
    int sda_gpio;
    int scl_gpio;
    uint32_t clock_hz;
    bool sh1106;
} ssd1306_config_t;

esp_err_t ssd1306_init(const ssd1306_config_t *config);
bool ssd1306_is_ready(void);
uint8_t ssd1306_address(void);
const char *ssd1306_controller_name(void);
void ssd1306_clear(void);
void ssd1306_fill_rect(int x, int y, int width, int height, bool color);
void ssd1306_draw_text(int x, int y, const char *text, bool color);
void ssd1306_draw_text_scaled(int x, int y, const char *text, uint8_t scale,
                              bool color);
void ssd1306_draw_text_scaled_xy(int x, int y, const char *text,
                                 uint8_t scale_x, uint8_t scale_y,
                                 bool color);
esp_err_t ssd1306_flush(void);

#ifdef __cplusplus
}
#endif
