#include "oled_canvas.h"

#include <stddef.h>
#include <string.h>

#include "third_party/u8g2/csrc/u8g2.h"

static uint8_t
    s_framebuffer[OLED_CANVAS_WIDTH * OLED_CANVAS_PAGE_COUNT];
static u8g2_t s_font_engine;
static oled_canvas_font_t s_current_font;
static bool s_font_engine_ready;

static const uint8_t *font_data(oled_canvas_font_t font)
{
    switch (font) {
    case OLED_CANVAS_FONT_VALUE:
        return u8g2_font_6x13B_tf;
    case OLED_CANVAS_FONT_WIDE:
        return u8g2_font_8x13B_tf;
    case OLED_CANVAS_FONT_DISPLAY:
        return u8g2_font_9x18B_tf;
    case OLED_CANVAS_FONT_SMALL:
    default:
        return u8g2_font_5x7_tf;
    }
}

void u8g2_DrawHVLine(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y,
                     u8g2_uint_t len, uint8_t dir)
{
    bool color = u8g2->draw_color != 0u;

    if (dir == 0u) {
        oled_canvas_fill_rect((int)x, (int)y, (int)len, 1, color);
    } else {
        oled_canvas_fill_rect((int)x, (int)y, 1, (int)len, color);
    }
}

uint8_t u8g2_IsIntersection(u8g2_t *u8g2, u8g2_uint_t x0,
                            u8g2_uint_t y0, u8g2_uint_t x1,
                            u8g2_uint_t y1)
{
    (void)u8g2;
    (void)x0;
    (void)y0;
    (void)x1;
    (void)y1;
    return 1u;
}

uint8_t u8g2_GetKerning(u8g2_t *u8g2, u8g2_kerning_t *kerning,
                        uint16_t e1, uint16_t e2)
{
    (void)u8g2;
    (void)kerning;
    (void)e1;
    (void)e2;
    return 0u;
}

uint8_t u8g2_GetKerningByTable(u8g2_t *u8g2, const uint16_t *table,
                               uint16_t e1, uint16_t e2)
{
    (void)u8g2;
    (void)table;
    (void)e1;
    (void)e2;
    return 0u;
}

void u8x8_utf8_init(u8x8_t *u8x8)
{
    (void)u8x8;
}

uint16_t u8x8_ascii_next(u8x8_t *u8x8, uint8_t value)
{
    (void)u8x8;
    return value == 0u ? 0x0ffffu : value;
}

uint16_t u8x8_utf8_next(u8x8_t *u8x8, uint8_t value)
{
    return u8x8_ascii_next(u8x8, value);
}

static void ensure_font_engine(void)
{
    if (s_font_engine_ready) {
        return;
    }
    memset(&s_font_engine, 0, sizeof(s_font_engine));
    s_font_engine.font_height_mode = U8G2_FONT_HEIGHT_MODE_TEXT;
    u8g2_SetFont(&s_font_engine, font_data(OLED_CANVAS_FONT_SMALL));
    u8g2_SetFontPosTop(&s_font_engine);
    u8g2_SetFontMode(&s_font_engine, 1u);
    s_font_engine.draw_color = 1u;
    s_current_font = OLED_CANVAS_FONT_SMALL;
    s_font_engine_ready = true;
}

void oled_canvas_clear(void)
{
    memset(s_framebuffer, 0, sizeof(s_framebuffer));
}

void oled_canvas_fill_rect(int x, int y, int width, int height,
                           bool color)
{
    int px;
    int py;

    if (width <= 0 || height <= 0) {
        return;
    }
    for (py = y; py < y + height; py++) {
        if (py < 0 || py >= OLED_CANVAS_HEIGHT) {
            continue;
        }
        for (px = x; px < x + width; px++) {
            uint16_t index;
            uint8_t mask;

            if (px < 0 || px >= OLED_CANVAS_WIDTH) {
                continue;
            }
            index = (uint16_t)((py / 8) * OLED_CANVAS_WIDTH + px);
            mask = (uint8_t)(1u << (py & 7));
            if (color) {
                s_framebuffer[index] |= mask;
            } else {
                s_framebuffer[index] &= (uint8_t)~mask;
            }
        }
    }
}

void oled_canvas_set_font(oled_canvas_font_t font)
{
    ensure_font_engine();
    u8g2_SetFont(&s_font_engine, font_data(font));
    s_current_font = font;
}

int oled_canvas_text_width(const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return 0;
    }
    ensure_font_engine();
    return (int)u8g2_GetStrWidth(&s_font_engine, text);
}

void oled_canvas_draw_text(int x, int y, const char *text, bool color)
{
    if (text == NULL) {
        return;
    }
    ensure_font_engine();
    s_font_engine.draw_color = color ? 1u : 0u;
    (void)u8g2_DrawStr(&s_font_engine, (u8g2_uint_t)x,
                       (u8g2_uint_t)y, text);
}

void oled_canvas_draw_text_font(int x, int y, const char *text,
                                oled_canvas_font_t font, bool color)
{
    oled_canvas_set_font(font);
    oled_canvas_draw_text(x, y, text, color);
}

const uint8_t *oled_canvas_framebuffer(void)
{
    return s_framebuffer;
}

size_t oled_canvas_framebuffer_size(void)
{
    return sizeof(s_framebuffer);
}
