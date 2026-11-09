#include "pir_input.h"

#include <stddef.h>

#include "driver/gpio.h"
#include "esp_check.h"

static esp_err_t validate_gpio(int gpio)
{
    if (gpio < 0 || !GPIO_IS_VALID_GPIO(gpio)) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t pir_input_init(pir_input_t *input, int gpio)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    if (input == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(validate_gpio(gpio), "pir", "invalid GPIO %d", gpio);
    ESP_RETURN_ON_ERROR(gpio_config(&config), "pir",
                        "GPIO %d configuration failed", gpio);

    input->gpio = gpio;
    input->initialized = true;
    return ESP_OK;
}

bool pir_input_read(const pir_input_t *input)
{
    if (input == NULL || !input->initialized) {
        return false;
    }

    return gpio_get_level((gpio_num_t)input->gpio) != 0;
}
