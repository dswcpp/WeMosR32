#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OLED_CANVAS_WIDTH 128
#define OLED_CANVAS_HEIGHT 64
#define OLED_CANVAS_PAGE_COUNT 8

typedef enum {
    OLED_CANVAS_FONT_SMALL = 0,
    OLED_CANVAS_FONT_VALUE,
    OLED_CANVAS_FONT_WIDE,
    OLED_CANVAS_FONT_DISPLAY,
} oled_canvas_font_t;

void oled_canvas_clear(void);
void oled_canvas_fill_rect(int x, int y, int width, int height,
                           bool color);
void oled_canvas_set_font(oled_canvas_font_t font);
int oled_canvas_text_width(const char *text);
void oled_canvas_draw_text(int x, int y, const char *text, bool color);
void oled_canvas_draw_text_font(int x, int y, const char *text,
                                oled_canvas_font_t font, bool color);
const uint8_t *oled_canvas_framebuffer(void);
size_t oled_canvas_framebuffer_size(void);

#ifdef __cplusplus
}
#endif
