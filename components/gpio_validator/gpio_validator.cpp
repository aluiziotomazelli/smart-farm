#include "gpio_validator.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_chip_info.h"
#include "esp_log.h"
#include "soc/soc_caps.h"

static const char *TAG = "GpioValidator";

esp_err_t GpioValidator::validate(gpio_num_t gpio, Mode mode)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    // 1. Basic validation: Is it a valid GPIO for this chip?
    if (gpio < 0 || gpio >= SOC_GPIO_PIN_COUNT) {
        ESP_LOGE(TAG, "GPIO %d is out of range", gpio);
        return ESP_ERR_INVALID_ARG;
    }

    if (!GPIO_IS_VALID_GPIO(gpio)) {
        ESP_LOGE(TAG, "GPIO %d is not a valid GPIO for this chip", gpio);
        return ESP_ERR_INVALID_ARG;
    }

    // 2. Mode validation
    if (mode == Mode::OUTPUT && !GPIO_IS_VALID_OUTPUT_GPIO(gpio)) {
        ESP_LOGE(TAG, "GPIO %d is NOT a valid OUTPUT GPIO", gpio);
        return ESP_ERR_INVALID_ARG;
    }

    // 3. Chip-specific logic
    // We use integer comparison and numeric literals to allow compilation on any
    // target (GPIO_NUM_X symbols might not be defined if the pin doesn't exist on
    // current target)
    int g = static_cast<int>(gpio);

    switch (chip_info.model) {
    case CHIP_ESP32:
    {
        // Prohibited: SPI Flash / PSRAM (6-11, 16, 17)
        if ((g >= 6 && g <= 11) || g == 16 || g == 17) {
            ESP_LOGE(TAG,
                     "GPIO %d is reserved for SPI Flash/PSRAM on ESP32 - PROHIBITED",
                     g);
            return ESP_ERR_INVALID_ARG;
        }
        // Warning: UART0 (1, 3)
        if (g == 1 || g == 3) {
            ESP_LOGW(TAG, "GPIO %d is used for UART0 (TX/RX) - use with caution", g);
        }
        // Warning: JTAG (12-15)
        if (g >= 12 && g <= 15) {
            ESP_LOGW(TAG, "GPIO %d is used for JTAG - use with caution", g);
        }
        // Warning: Strapping pins (0, 2, 5, 12, 15)
        if (g == 0 || g == 2 || g == 5 || g == 12 || g == 15) {
            ESP_LOGW(TAG, "GPIO %d is a strapping pin - may affect boot mode", g);
        }
        if (g >= 34 && g <= 39 && mode == Mode::INPUT) {
            ESP_LOGW(TAG, "GPIO %d has no pullup/pulldown support", g);
            return ESP_OK;
        }
        break;
    }

    case CHIP_ESP32S3:
    {
        // Prohibited: SPI Flash (26-32)
        if (g >= 26 && g <= 32) {
            ESP_LOGE(TAG,
                     "GPIO %d is reserved for SPI Flash on ESP32-S3 - PROHIBITED",
                     g);
            return ESP_ERR_INVALID_ARG;
        }
        // Prohibited: Octal Flash (33-37)
        if (g >= 33 && g <= 37) {
            ESP_LOGE(TAG,
                     "GPIO %d is reserved for Octal Flash on ESP32-S3 - PROHIBITED",
                     g);
            return ESP_ERR_INVALID_ARG;
        }
        // Warning: USB-JTAG (19, 20)
        if (g == 19 || g == 20) {
            ESP_LOGW(TAG, "GPIO %d is used for USB-JTAG - use with caution", g);
        }
        // Warning: UART0 (43, 44)
        if (g == 43 || g == 44) {
            ESP_LOGW(TAG, "GPIO %d is used for UART0 (TX/RX) - use with caution", g);
        }
        // Warning: Strapping pins (0, 3, 45, 46)
        if (g == 0 || g == 3 || g == 45 || g == 46) {
            ESP_LOGW(TAG, "GPIO %d is a strapping pin - may affect boot mode", g);
        }
        break;
    }

    case CHIP_ESP32C3:
    {
        // Prohibited: SPI Flash (12-17)
        if (g >= 12 && g <= 17) {
            ESP_LOGE(TAG,
                     "GPIO %d is reserved for SPI Flash on ESP32-C3 - PROHIBITED",
                     g);
            return ESP_ERR_INVALID_ARG;
        }
        // Warning: USB-JTAG (18, 19)
        if (g == 18 || g == 19) {
            ESP_LOGW(TAG, "GPIO %d is used for USB-JTAG - use with caution", g);
        }
        // Warning: Strapping pins (2, 8, 9)
        if (g == 2 || g == 8 || g == 9) {
            ESP_LOGW(TAG, "GPIO %d is a strapping pin - may affect boot mode", g);
        }
        break;
    }

    default:
        ESP_LOGW(TAG,
                 "Chip model %d not explicitly handled, performing basic validation",
                 chip_info.model);
        break;
    }

    return ESP_OK;
}
