#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "oled_canvas.h"

#define OLED_DISPLAY_WIDTH OLED_CANVAS_WIDTH
#define OLED_DISPLAY_HEIGHT OLED_CANVAS_HEIGHT
#define OLED_DISPLAY_PAGE_COUNT OLED_CANVAS_PAGE_COUNT

typedef enum {
    OLED_DISPLAY_CONTROLLER_SSD1306 = 0,
    OLED_DISPLAY_CONTROLLER_SH1106,
} oled_display_controller_t;

typedef struct {
    int i2c_port;
    int sda_gpio;
    int scl_gpio;
    uint32_t clock_hz;
    oled_display_controller_t controller;
} oled_display_config_t;

esp_err_t oled_display_init(const oled_display_config_t *config);
bool oled_display_is_ready(void);
uint8_t oled_display_address(void);
oled_display_controller_t oled_display_controller(void);
void oled_display_clear(void);
void oled_display_fill_rect(int x, int y, int width, int height,
                            bool color);
void oled_display_set_font(oled_canvas_font_t font);
int oled_display_text_width(const char *text);
void oled_display_draw_text(int x, int y, const char *text, bool color);
void oled_display_draw_text_font(int x, int y, const char *text,
                                 oled_canvas_font_t font, bool color);
esp_err_t oled_display_flush(void);

#ifdef __cplusplus
}
#endif
