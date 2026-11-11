#include "oled_display.h"

#include <stddef.h>

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define OLED_DISPLAY_TAG "oled"
#define OLED_DISPLAY_PRIMARY_ADDRESS 0x3Cu
#define OLED_DISPLAY_SECONDARY_ADDRESS 0x3Du
#define OLED_DISPLAY_TRANSFER_TIMEOUT_MS 50

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_device;
static uint8_t s_address;
static oled_display_controller_t s_controller;
static bool s_ready;

static void release_device(void)
{
    if (s_device != NULL) {
        (void)i2c_master_bus_rm_device(s_device);
        s_device = NULL;
    }
    s_address = 0u;
    s_controller = OLED_DISPLAY_CONTROLLER_SSD1306;
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
        s_device, buffers, 2u, OLED_DISPLAY_TRANSFER_TIMEOUT_MS);
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
        OLED_DISPLAY_TAG, "I2C device add failed");
    s_address = address;
    return ESP_OK;
}

static esp_err_t initialize_panel(
    uint32_t clock_hz, oled_display_controller_t controller)
{
    static const uint8_t common_setup[] = {
        0xAE,       /* display off */
        0xD5, 0x80, /* clock divide */
        0xA8, 0x3F, /* multiplex 1:64 */
        0xD3, 0x00, /* display offset */
        0x40,       /* start line 0 */
    };
    static const uint8_t ssd1306_power[] = {
        0x8D, 0x14, /* charge pump enabled */
    };
    static const uint8_t sh1106_power[] = {
        0xAD, 0x8B, /* DC-DC converter enabled */
    };
    static const uint8_t common_finish[] = {
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
    static const uint8_t ssd1306_addressing[] = {
        0x20, 0x02, /* page addressing */
    };
    esp_err_t err;

    err = i2c_master_probe(s_bus, OLED_DISPLAY_PRIMARY_ADDRESS,
                           OLED_DISPLAY_TRANSFER_TIMEOUT_MS);
    if (err == ESP_OK) {
        ESP_RETURN_ON_ERROR(
            add_device(OLED_DISPLAY_PRIMARY_ADDRESS, clock_hz),
            OLED_DISPLAY_TAG,
            "primary address setup failed");
    } else {
        err = i2c_master_probe(s_bus, OLED_DISPLAY_SECONDARY_ADDRESS,
                               OLED_DISPLAY_TRANSFER_TIMEOUT_MS);
        if (err == ESP_OK) {
            ESP_RETURN_ON_ERROR(
                add_device(OLED_DISPLAY_SECONDARY_ADDRESS, clock_hz),
                OLED_DISPLAY_TAG,
                "secondary address setup failed");
        } else {
            return err;
        }
    }

    err = write_commands(common_setup, sizeof(common_setup));
    if (err != ESP_OK) {
        return err;
    }
    err = controller == OLED_DISPLAY_CONTROLLER_SH1106
              ? write_commands(sh1106_power, sizeof(sh1106_power))
              : write_commands(ssd1306_power, sizeof(ssd1306_power));
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    err = write_commands(common_finish, sizeof(common_finish) -
                                             sizeof((uint8_t[]){0xAF}));
    if (err == ESP_OK && controller == OLED_DISPLAY_CONTROLLER_SSD1306) {
        err = write_commands(ssd1306_addressing,
                             sizeof(ssd1306_addressing));
    }
    if (err == ESP_OK) {
        const uint8_t display_on = 0xAF;

        err = write_commands(&display_on, 1u);
    }
    if (err != ESP_OK) {
        return err;
    }

    s_controller = controller;
    s_ready = true;
    oled_canvas_clear();
    return oled_display_flush();
}

esp_err_t oled_display_init(const oled_display_config_t *config)
{
    i2c_master_bus_config_t bus_config;
    esp_err_t err;

    if (config == NULL || config->i2c_port < 0 ||
        config->i2c_port > 1 || config->clock_hz == 0u) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->controller != OLED_DISPLAY_CONTROLLER_SSD1306 &&
        config->controller != OLED_DISPLAY_CONTROLLER_SH1106) {
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

    release_device();
    if (s_bus == NULL) {
        ESP_LOGI(OLED_DISPLAY_TAG,
                 "initializing I2C%d with SDA=GPIO%d SCL=GPIO%d clock=%lu Hz",
                 config->i2c_port, config->sda_gpio, config->scl_gpio,
                 (unsigned long)config->clock_hz);
        err = i2c_new_master_bus(&bus_config, &s_bus);
        if (err != ESP_OK) {
            return err;
        }
    }

    err = initialize_panel(config->clock_hz, config->controller);
    if (err != ESP_OK) {
        release_device();
    }
    return err;
}

bool oled_display_is_ready(void)
{
    return s_ready;
}

uint8_t oled_display_address(void)
{
    return s_ready ? s_address : 0u;
}

oled_display_controller_t oled_display_controller(void)
{
    return s_ready ? s_controller : OLED_DISPLAY_CONTROLLER_SSD1306;
}

void oled_display_clear(void)
{
    oled_canvas_clear();
}

void oled_display_fill_rect(int x, int y, int width, int height,
                            bool color)
{
    oled_canvas_fill_rect(x, y, width, height, color);
}

void oled_display_set_font(oled_canvas_font_t font)
{
    oled_canvas_set_font(font);
}

int oled_display_text_width(const char *text)
{
    return oled_canvas_text_width(text);
}

void oled_display_draw_text(int x, int y, const char *text, bool color)
{
    oled_canvas_draw_text(x, y, text, color);
}

void oled_display_draw_text_font(int x, int y, const char *text,
                                 oled_canvas_font_t font, bool color)
{
    oled_canvas_draw_text_font(x, y, text, font, color);
}

esp_err_t oled_display_flush(void)
{
    uint8_t page;

    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    for (page = 0u; page < OLED_DISPLAY_PAGE_COUNT; page++) {
        const uint8_t position[3] = {
            (uint8_t)(0xB0u | page),
            s_controller == OLED_DISPLAY_CONTROLLER_SH1106 ? 0x02u : 0x00u,
            0x10u,
        };
        esp_err_t err = write_commands(position, sizeof(position));

        if (err != ESP_OK) {
            return err;
        }
        err = write_stream(
            0x40u, &oled_canvas_framebuffer()[page * OLED_CANVAS_WIDTH],
            OLED_CANVAS_WIDTH);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
