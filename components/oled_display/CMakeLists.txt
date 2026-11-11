idf_component_register(
    SRCS
        "oled_canvas.c"
        "oled_display.c"
        "third_party/u8g2/csrc/u8g2_font.c"
        "third_party/u8g2/fonts/u8g2_font_5x7_tf.c"
        "third_party/u8g2/fonts/u8g2_font_6x13B_tf.c"
        "third_party/u8g2/fonts/u8g2_font_8x13B_tf.c"
        "third_party/u8g2/fonts/u8g2_font_9x18B_tf.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "third_party/u8g2/csrc"
    PRIV_REQUIRES
        esp_driver_i2c
        freertos
)
