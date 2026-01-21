#pragma once

#include "esp_err.h"
#include "driver/gpio.h"

/**
 * @brief Utility class to validate GPIO pins based on chip type and usage mode.
 *
 * This class provides a centralized way to check if a GPIO pin is safe to use,
 * avoiding pins reserved for SPI flash, PSRAM, or other critical functions.
 */
class GpioValidator
{
public:
    /**
     * @brief GPIO usage mode for validation.
     */
    enum class Mode
    {
        INPUT, ///< Validate for use as input
        OUTPUT ///< Validate for use as output
    };

    /**
     * @brief Validates a GPIO pin for a specific mode.
     *
     * This function checks the current chip information and applies specific rules
     * for ESP32, ESP32-S3, and ESP32-C3.
     *
     * - Returns ESP_ERR_INVALID_ARG for prohibited pins (e.g., SPI flash).
     * - Returns ESP_ERR_INVALID_ARG if the pin does not support the requested mode.
     * - Logs warnings for pins with special functions (JTAG, UART0, Boot).
     *
     * @param gpio The GPIO number to validate.
     * @param mode The intended usage mode (INPUT or OUTPUT).
     * @return
     *      - ESP_OK: Pin is safe to use.
     *      - ESP_ERR_INVALID_ARG: Pin is prohibited or doesn't support the mode.
     */
    static esp_err_t validate(gpio_num_t gpio, Mode mode);
};
