#include "ssd1306.h"

#include <stddef.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SSD1306_TAG "ssd1306"
#define SSD1306_PRIMARY_ADDRESS 0x3Cu
#define SSD1306_SECONDARY_ADDRESS 0x3Du
#define SSD1306_FONT_WIDTH 5
#define SSD1306_FONT_HEIGHT 7
#define SSD1306_FONT_FIRST ' '
#define SSD1306_FONT_LAST 'Z'
#define SSD1306_TRANSFER_TIMEOUT_MS 50

static uint8_t s_framebuffer[SSD1306_WIDTH * SSD1306_PAGE_COUNT];
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_device;
static uint8_t s_address;
static bool s_ready;
static bool s_sh1106;

static const uint8_t
    s_font[SSD1306_FONT_LAST - SSD1306_FONT_FIRST + 1]
          [SSD1306_FONT_HEIGHT] = {
    ['!' - SSD1306_FONT_FIRST] = {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04},
    ['%' - SSD1306_FONT_FIRST] = {0x19, 0x1A, 0x04, 0x08, 0x16, 0x13, 0x00},
    ['-' - SSD1306_FONT_FIRST] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00},
    ['.' - SSD1306_FONT_FIRST] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04},
    ['/' - SSD1306_FONT_FIRST] = {0x01, 0x02, 0x04, 0x04, 0x08, 0x10, 0x00},
    [':' - SSD1306_FONT_FIRST] = {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00},
    ['?' - SSD1306_FONT_FIRST] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04},
    ['0' - SSD1306_FONT_FIRST] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
    ['1' - SSD1306_FONT_FIRST] = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    ['2' - SSD1306_FONT_FIRST] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
    ['3' - SSD1306_FONT_FIRST] = {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},
    ['4' - SSD1306_FONT_FIRST] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
    ['5' - SSD1306_FONT_FIRST] = {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
    ['6' - SSD1306_FONT_FIRST] = {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},
    ['7' - SSD1306_FONT_FIRST] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    ['8' - SSD1306_FONT_FIRST] = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
    ['9' - SSD1306_FONT_FIRST] = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
    ['A' - SSD1306_FONT_FIRST] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
    ['B' - SSD1306_FONT_FIRST] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},
    ['C' - SSD1306_FONT_FIRST] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E},
    ['D' - SSD1306_FONT_FIRST] = {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E},
    ['E' - SSD1306_FONT_FIRST] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F},
    ['F' - SSD1306_FONT_FIRST] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
    ['G' - SSD1306_FONT_FIRST] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F},
    ['H' - SSD1306_FONT_FIRST] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
    ['I' - SSD1306_FONT_FIRST] = {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E},
    ['J' - SSD1306_FONT_FIRST] = {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C},
    ['K' - SSD1306_FONT_FIRST] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
    ['L' - SSD1306_FONT_FIRST] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
    ['M' - SSD1306_FONT_FIRST] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11},
    ['N' - SSD1306_FONT_FIRST] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},
    ['O' - SSD1306_FONT_FIRST] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
    ['P' - SSD1306_FONT_FIRST] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
    ['Q' - SSD1306_FONT_FIRST] = {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D},
    ['R' - SSD1306_FONT_FIRST] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
    ['S' - SSD1306_FONT_FIRST] = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E},
    ['T' - SSD1306_FONT_FIRST] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
    ['U' - SSD1306_FONT_FIRST] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
    ['V' - SSD1306_FONT_FIRST] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},
    ['W' - SSD1306_FONT_FIRST] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11},
    ['X' - SSD1306_FONT_FIRST] = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
    ['Y' - SSD1306_FONT_FIRST] = {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04},
    ['Z' - SSD1306_FONT_FIRST] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F},
};

static void release_device(void)
{
    if (s_device != NULL) {
        (void)i2c_master_bus_rm_device(s_device);
        s_device = NULL;
    }
    s_address = 0u;
    s_ready = false;
}

static esp_err_t write_stream(uint8_t control, const uint8_t *data,
                              size_t length)
{
    i2c_master_transmit_multi_buffer_info_t buffers[2] = {
        {.write_buffer = &control, .buffer_size = 1u},
        {.write_buffer = data, .buffer_size = length},
    };
    esp_err_t err;

    if (s_device == NULL || data == NULL || length == 0u) {
        return ESP_ERR_INVALID_STATE;
    }

    err = i2c_master_multi_buffer_transmit(
        s_device, buffers, 2u, SSD1306_TRANSFER_TIMEOUT_MS);
    if (err != ESP_OK) {
        release_device();
    }
    return err;
}

static esp_err_t write_commands(const uint8_t *commands, size_t length)
{
    return write_stream(0x00u, commands, length);
}

static esp_err_t add_device(uint8_t address, uint32_t clock_hz)
{
    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = clock_hz,
        .scl_wait_us = 0u,
        .flags.disable_ack_check = false,
    };

    ESP_RETURN_ON_ERROR(
        i2c_master_bus_add_device(s_bus, &device_config, &s_device),
        "ssd1306", "I2C device add failed");
    s_address = address;
    return ESP_OK;
}

static esp_err_t initialize_panel(uint32_t clock_hz)
{
    static const uint8_t setup[] = {
        0xAE,       /* display off */
        0xD5, 0x80, /* clock divide */
        0xA8, 0x3F, /* multiplex 1:64 */
        0xD3, 0x00, /* display offset */
        0x40,       /* start line 0 */
        0x8D, 0x14, /* charge pump enabled */
    };
    static const uint8_t ssd1306_page_addressing[] = {
        0x20, 0x02,
    };
    static const uint8_t finish[] = {
        0xA1,       /* segment remap */
        0xC8,       /* COM scan direction */
        0xDA, 0x12, /* COM pins */
        0x81, 0xCF, /* contrast */
        0xD9, 0xF1, /* pre-charge */
        0xDB, 0x40, /* VCOMH */
        0xA4,       /* display follows RAM */
        0xA6,       /* normal display */
        0x2E,       /* stop scroll */
        0xAF,       /* display on */
    };
    esp_err_t err;

    err = i2c_master_probe(s_bus, SSD1306_PRIMARY_ADDRESS,
                           SSD1306_TRANSFER_TIMEOUT_MS);
    if (err == ESP_OK) {
        ESP_RETURN_ON_ERROR(
            add_device(SSD1306_PRIMARY_ADDRESS, clock_hz), "ssd1306",
            "primary address setup failed");
    } else {
        err = i2c_master_probe(s_bus, SSD1306_SECONDARY_ADDRESS,
                               SSD1306_TRANSFER_TIMEOUT_MS);
        if (err == ESP_OK) {
            ESP_RETURN_ON_ERROR(
                add_device(SSD1306_SECONDARY_ADDRESS, clock_hz), "ssd1306",
                "secondary address setup failed");
        } else {
            return err;
        }
    }

    err = write_commands(setup, sizeof(setup));
    if (err != ESP_OK) {
        return err;
    }
    if (!s_sh1106) {
        err = write_commands(ssd1306_page_addressing,
                             sizeof(ssd1306_page_addressing));
        if (err != ESP_OK) {
            return err;
        }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    err = write_commands(finish, sizeof(finish));
    if (err != ESP_OK) {
        return err;
    }

    s_ready = true;
    ssd1306_clear();
    return ssd1306_flush();
}

esp_err_t ssd1306_init(const ssd1306_config_t *config)
{
    i2c_master_bus_config_t bus_config;
    esp_err_t err;

    if (config == NULL || config->i2c_port < 0 ||
        config->i2c_port > 1 || config->clock_hz == 0u) {
        return ESP_ERR_INVALID_ARG;
    }
    bus_config = (i2c_master_bus_config_t){
        .i2c_port = (i2c_port_num_t)config->i2c_port,
        .sda_io_num = (gpio_num_t)config->sda_gpio,
        .scl_io_num = (gpio_num_t)config->scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7u,
        .intr_priority = 0,
        .trans_queue_depth = 0u,
        .flags.enable_internal_pullup = true,
        .flags.allow_pd = false,
    };

    s_sh1106 = config->sh1106;
    release_device();
    if (s_bus == NULL) {
        ESP_LOGI(SSD1306_TAG,
                 "initializing I2C%d with SDA=GPIO%d SCL=GPIO%d clock=%lu Hz",
                 config->i2c_port, config->sda_gpio, config->scl_gpio,
                 (unsigned long)config->clock_hz);
        err = i2c_new_master_bus(&bus_config, &s_bus);
        if (err != ESP_OK) {
            return err;
        }
    }

    err = initialize_panel(config->clock_hz);
    if (err != ESP_OK) {
        release_device();
    }
    return err;
}

bool ssd1306_is_ready(void)
{
    return s_ready;
}

uint8_t ssd1306_address(void)
{
    return s_ready ? s_address : 0u;
}

const char *ssd1306_controller_name(void)
{
    return s_sh1106 ? "sh1106" : "ssd1306";
}

void ssd1306_clear(void)
{
    memset(s_framebuffer, 0, sizeof(s_framebuffer));
}

void ssd1306_fill_rect(int x, int y, int width, int height, bool color)
{
    int px;
    int py;

    if (width <= 0 || height <= 0) {
        return;
    }
    for (py = y; py < y + height; py++) {
        if (py < 0 || py >= SSD1306_HEIGHT) {
            continue;
        }
        for (px = x; px < x + width; px++) {
            uint16_t index;
            uint8_t mask;

            if (px < 0 || px >= SSD1306_WIDTH) {
                continue;
            }
            index = (uint16_t)((py / 8) * SSD1306_WIDTH + px);
            mask = (uint8_t)(1u << (py & 7));
            if (color) {
                s_framebuffer[index] |= mask;
            } else {
                s_framebuffer[index] &= (uint8_t)~mask;
            }
        }
    }
}

void ssd1306_draw_text_scaled_xy(int x, int y, const char *text,
                                 uint8_t scale_x, uint8_t scale_y,
                                 bool color)
{
    const uint8_t *glyph;
    int row;
    int column;
    int pixel_x;
    int pixel_y;
    char character;

    if (text == NULL || scale_x == 0u || scale_y == 0u) {
        return;
    }

    while (*text != '\0') {
        character = *text++;
        if (character >= 'a' && character <= 'z') {
            character = (char)(character - 'a' + 'A');
        }
        if (character < SSD1306_FONT_FIRST ||
            character > SSD1306_FONT_LAST) {
            character = '?';
        }

        glyph = s_font[(uint8_t)character - SSD1306_FONT_FIRST];
        for (row = 0; row < SSD1306_FONT_HEIGHT; row++) {
            for (column = 0; column < SSD1306_FONT_WIDTH; column++) {
                bool foreground =
                    (glyph[row] &
                     (uint8_t)(1u << (SSD1306_FONT_WIDTH - 1 - column))) != 0;
                for (pixel_y = 0; pixel_y < (int)scale_y; pixel_y++) {
                    for (pixel_x = 0; pixel_x < (int)scale_x; pixel_x++) {
                        ssd1306_fill_rect(
                            x + column * (int)scale_x + pixel_x,
                            y + row * (int)scale_y + pixel_y, 1, 1,
                            foreground ? color : !color);
                    }
                }
            }
        }
        for (row = 0; row < SSD1306_FONT_HEIGHT; row++) {
            for (pixel_y = 0; pixel_y < (int)scale_y; pixel_y++) {
                for (pixel_x = 0; pixel_x < (int)scale_x; pixel_x++) {
                    ssd1306_fill_rect(
                        x + SSD1306_FONT_WIDTH * (int)scale_x + pixel_x,
                        y + row * (int)scale_y + pixel_y, 1, 1, !color);
                }
            }
        }

        x += SSD1306_FONT_ADVANCE * (int)scale_x;
        if (x >= SSD1306_WIDTH) {
            break;
        }
    }
}

void ssd1306_draw_text_scaled(int x, int y, const char *text, uint8_t scale,
                              bool color)
{
    ssd1306_draw_text_scaled_xy(x, y, text, scale, scale, color);
}

void ssd1306_draw_text(int x, int y, const char *text, bool color)
{
    ssd1306_draw_text_scaled(x, y, text, 1u, color);
}

esp_err_t ssd1306_flush(void)
{
    uint8_t page;
    const uint8_t column_offset = s_sh1106 ? 2u : 0u;

    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    for (page = 0u; page < SSD1306_PAGE_COUNT; page++) {
        const uint8_t position[3] = {
            (uint8_t)(0xB0u | page),
            (uint8_t)(column_offset & 0x0Fu),
            (uint8_t)(0x10u | (column_offset >> 4u)),
        };
        esp_err_t err = write_commands(position, sizeof(position));

        if (err != ESP_OK) {
            return err;
        }
        err = write_stream(0x40u, &s_framebuffer[page * SSD1306_WIDTH],
                           SSD1306_WIDTH);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
