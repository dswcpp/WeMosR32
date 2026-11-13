#include "button_input.h"

#include <stddef.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "buttons";

static bool input_has_internal_pullup(int gpio)
{
    return GPIO_IS_VALID_OUTPUT_GPIO(gpio);
}

esp_err_t button_input_init(button_input_t *input,
                            const int gpios[BUTTON_INPUT_COUNT])
{
    if (input == NULL || gpios == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *input = (button_input_t){0};
    for (uint8_t index = 0u; index < BUTTON_INPUT_COUNT; index++) {
        int gpio = gpios[index];
        gpio_config_t config;

        if (gpio < 0 || !GPIO_IS_VALID_GPIO(gpio)) {
            return ESP_ERR_INVALID_ARG;
        }
        for (uint8_t previous = 0u; previous < index; previous++) {
            if (gpios[previous] == gpio) {
                return ESP_ERR_INVALID_ARG;
            }
        }
        config = (gpio_config_t){
            .pin_bit_mask = 1ULL << gpio,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&config), TAG,
                            "K%u GPIO%d configuration failed",
                            (unsigned)(index + 1u), gpio);
        if (input_has_internal_pullup(gpio)) {
            ESP_RETURN_ON_ERROR(gpio_set_pull_mode(
                                    (gpio_num_t)gpio, GPIO_PULLUP_ONLY),
                                TAG,
                                "K%u GPIO%d pull-up setup failed",
                                (unsigned)(index + 1u), gpio);
        } else {
            ESP_LOGW(TAG,
                     "K%u GPIO%d has no internal pull-up; fit 10 kOhm to 3.3 V",
                     (unsigned)(index + 1u), gpio);
        }
        input->gpios[index] = gpio;
    }

    input->initialized = true;
    return ESP_OK;
}

uint8_t button_input_read_mask(const button_input_t *input)
{
    uint8_t mask = 0u;

    if (input == NULL || !input->initialized) {
        return 0u;
    }

    for (uint8_t index = 0u; index < BUTTON_INPUT_COUNT; index++) {
        if (gpio_get_level((gpio_num_t)input->gpios[index]) == 0) {
            mask |= (uint8_t)(1u << index);
        }
    }
    return mask;
}
