#include "gpio_validator.hpp"
#include "unity.h"
#include "esp_log.h"
#include "esp_chip_info.h"

static const char *TAG = "GpioValidatorTest";

TEST_CASE("GpioValidator: Basic valid GPIOs", "[gpio][validator]")
{
    // GPIO 2 is usually valid on most chips (LED)
    TEST_ASSERT_EQUAL(ESP_OK, GpioValidator::validate(GPIO_NUM_2, GpioValidator::Mode::OUTPUT));
    TEST_ASSERT_EQUAL(ESP_OK, GpioValidator::validate(GPIO_NUM_2, GpioValidator::Mode::INPUT));

    // GPIO 4 is usually valid
    TEST_ASSERT_EQUAL(ESP_OK, GpioValidator::validate(GPIO_NUM_4, GpioValidator::Mode::OUTPUT));
}

TEST_CASE("GpioValidator: Chip-specific Flash pins (Negative)", "[gpio][validator][negative]")
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    if (chip_info.model == CHIP_ESP32) {
        ESP_LOGI(TAG, "Running ESP32-specific flash pin tests");
        // Pins 6-11 are flash
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, GpioValidator::validate((gpio_num_t)6, GpioValidator::Mode::OUTPUT));
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, GpioValidator::validate((gpio_num_t)11, GpioValidator::Mode::OUTPUT));
        // Pins 16-17 are often PSRAM/Flash on some modules
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, GpioValidator::validate((gpio_num_t)16, GpioValidator::Mode::OUTPUT));
    }
    else if (chip_info.model == CHIP_ESP32S3) {
        ESP_LOGI(TAG, "Running ESP32-S3-specific flash pin tests");
        // Pins 26-32 are flash
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, GpioValidator::validate((gpio_num_t)26, GpioValidator::Mode::OUTPUT));
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, GpioValidator::validate((gpio_num_t)32, GpioValidator::Mode::OUTPUT));
    }
    else if (chip_info.model == CHIP_ESP32C3) {
        ESP_LOGI(TAG, "Running ESP32-C3-specific flash pin tests");
        // Pins 12-17 are flash
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, GpioValidator::validate((gpio_num_t)12, GpioValidator::Mode::OUTPUT));
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, GpioValidator::validate((gpio_num_t)17, GpioValidator::Mode::OUTPUT));
    }
}

TEST_CASE("GpioValidator: Input-only pins (Negative Output)", "[gpio][validator][negative]")
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    if (chip_info.model == CHIP_ESP32) {
        // Pins 34-39 are GPI (input only)
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, GpioValidator::validate((gpio_num_t)34, GpioValidator::Mode::OUTPUT));
        TEST_ASSERT_EQUAL(ESP_OK, GpioValidator::validate((gpio_num_t)34, GpioValidator::Mode::INPUT));
    }
}

TEST_CASE("GpioValidator: Warnings for dedicated pins", "[gpio][validator]")
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    // These should return ESP_OK but log warnings
    if (chip_info.model == CHIP_ESP32) {
        // Strapping / JTAG
        TEST_ASSERT_EQUAL(ESP_OK, GpioValidator::validate((gpio_num_t)0, GpioValidator::Mode::OUTPUT));
        TEST_ASSERT_EQUAL(ESP_OK, GpioValidator::validate((gpio_num_t)12, GpioValidator::Mode::OUTPUT));
        // UART0
        TEST_ASSERT_EQUAL(ESP_OK, GpioValidator::validate((gpio_num_t)1, GpioValidator::Mode::OUTPUT));
    }
    else if (chip_info.model == CHIP_ESP32S3) {
        // USB-JTAG
        TEST_ASSERT_EQUAL(ESP_OK, GpioValidator::validate((gpio_num_t)19, GpioValidator::Mode::OUTPUT));
        // Strapping
        TEST_ASSERT_EQUAL(ESP_OK, GpioValidator::validate((gpio_num_t)0, GpioValidator::Mode::OUTPUT));
    }
}

TEST_CASE("GpioValidator: Out of range GPIOs", "[gpio][validator][negative]")
{
    // Test with a GPIO that definitely doesn't exist
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, GpioValidator::validate((gpio_num_t)100, GpioValidator::Mode::OUTPUT));
}
