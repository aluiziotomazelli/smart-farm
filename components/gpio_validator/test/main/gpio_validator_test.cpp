// gpio_validator_test.cpp
#include "esp_chip_info.h"
#include "esp_log.h"
#include "gpio_validator.hpp"
#include "unity.h"

static const char *TAG = "GpioValidatorTest";

TEST_CASE("GpioValidator: Basic valid GPIOs", "[gpio_validator][basic]")
{
    ESP_LOGI(TAG, "Testing basic valid GPIOs");

    // Common valid GPIOs for most ESP chips
    gpio_num_t valid_gpios[] = {GPIO_NUM_2, GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_13,
                                GPIO_NUM_14};

    for (auto gpio : valid_gpios) {
        ESP_LOGI(TAG, "Validating GPIO %d", gpio);

        // Should be valid for output
        TEST_ASSERT_EQUAL(
            ESP_OK, GpioValidator::validate(gpio, GpioValidator::Mode::OUTPUT));

        // Should be valid for input
        TEST_ASSERT_EQUAL(ESP_OK,
                          GpioValidator::validate(gpio, GpioValidator::Mode::INPUT));
    }
}

TEST_CASE("GpioValidator: Out of range GPIOs", "[gpio][validator][negative]")
{
    // Test with a GPIO that definitely doesn't exist
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        GpioValidator::validate((gpio_num_t)100, GpioValidator::Mode::OUTPUT));
}
