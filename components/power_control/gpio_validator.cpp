#include "gpio_validator.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"
#include "esp_chip_info.h"

static const char *TAG = "GpioValidator";

esp_err_t GpioValidator::validate(gpio_num_t gpio, Mode mode)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    // 1. Basic validation: Is it a valid GPIO for this chip?
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
    switch (chip_info.model) {
    case CHIP_ESP32: {
        // Prohibited: SPI Flash / PSRAM
        if ((gpio >= GPIO_NUM_6 && gpio <= GPIO_NUM_11) || gpio == GPIO_NUM_16 || gpio == GPIO_NUM_17) {
            ESP_LOGE(TAG, "GPIO %d is reserved for SPI Flash/PSRAM on ESP32 - PROHIBITED", gpio);
            return ESP_ERR_INVALID_ARG;
        }
        // Warning: UART0
        if (gpio == GPIO_NUM_1 || gpio == GPIO_NUM_3) {
            ESP_LOGW(TAG, "GPIO %d is used for UART0 (TX/RX) - use with caution", gpio);
        }
        // Warning: JTAG
        if (gpio >= GPIO_NUM_12 && gpio <= GPIO_NUM_15) {
            ESP_LOGW(TAG, "GPIO %d is used for JTAG - use with caution", gpio);
        }
        // Warning: Strapping pins
        if (gpio == GPIO_NUM_0 || gpio == GPIO_NUM_2 || gpio == GPIO_NUM_5 || gpio == GPIO_NUM_12 || gpio == GPIO_NUM_15) {
            ESP_LOGW(TAG, "GPIO %d is a strapping pin - may affect boot mode", gpio);
        }
        break;
    }

    case CHIP_ESP32S3: {
        // Prohibited: SPI Flash
        if (gpio >= GPIO_NUM_26 && gpio <= GPIO_NUM_32) {
            ESP_LOGE(TAG, "GPIO %d is reserved for SPI Flash on ESP32-S3 - PROHIBITED", gpio);
            return ESP_ERR_INVALID_ARG;
        }
        // Prohibited: Octal Flash (highly sensitive)
        if (gpio >= GPIO_NUM_33 && gpio <= GPIO_NUM_37) {
            ESP_LOGE(TAG, "GPIO %d is reserved for Octal Flash on ESP32-S3 - PROHIBITED", gpio);
            return ESP_ERR_INVALID_ARG;
        }
        // Warning: USB-JTAG
        if (gpio == GPIO_NUM_19 || gpio == GPIO_NUM_20) {
            ESP_LOGW(TAG, "GPIO %d is used for USB-JTAG - use with caution", gpio);
        }
        // Warning: UART0
        if (gpio == GPIO_NUM_43 || gpio == GPIO_NUM_44) {
            ESP_LOGW(TAG, "GPIO %d is used for UART0 (TX/RX) - use with caution", gpio);
        }
        // Warning: Strapping pins
        if (gpio == GPIO_NUM_0 || gpio == GPIO_NUM_3 || gpio == GPIO_NUM_45 || gpio == GPIO_NUM_46) {
            ESP_LOGW(TAG, "GPIO %d is a strapping pin - may affect boot mode", gpio);
        }
        break;
    }

    case CHIP_ESP32C3: {
        // Prohibited: SPI Flash
        if (gpio >= GPIO_NUM_12 && gpio <= GPIO_NUM_17) {
            ESP_LOGE(TAG, "GPIO %d is reserved for SPI Flash on ESP32-C3 - PROHIBITED", gpio);
            return ESP_ERR_INVALID_ARG;
        }
        // Warning: USB-JTAG
        if (gpio == GPIO_NUM_18 || gpio == GPIO_NUM_19) {
            ESP_LOGW(TAG, "GPIO %d is used for USB-JTAG - use with caution", gpio);
        }
        // Warning: UART0
        if (gpio == GPIO_NUM_20 || gpio == GPIO_NUM_21) {
            ESP_LOGW(TAG, "GPIO %d is used for UART0 (TX/RX) - use with caution", gpio);
        }
        // Warning: Strapping pins
        if (gpio == GPIO_NUM_2 || gpio == GPIO_NUM_8 || gpio == GPIO_NUM_9) {
            ESP_LOGW(TAG, "GPIO %d is a strapping pin - may affect boot mode", gpio);
        }
        break;
    }

    default:
        ESP_LOGW(TAG, "Chip model %d not explicitly handled, performing basic validation", chip_info.model);
        break;
    }

    return ESP_OK;
}
