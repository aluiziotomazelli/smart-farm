#include "unity.h"
#include "gpio_validator.hpp"
#include "esp_chip_info.h"
#include <stdio.h>

// Note: In a real CMock setup, these functions would be provided by the generated mock.
// Since we don't have Ruby/CMock running here, we simulate the behavior by manually
// defining the functions for this demonstration.

static esp_chip_model_t simulated_chip = CHIP_ESP32;

// Simulated/Mocked version of esp_chip_info
extern "C" void esp_chip_info(esp_chip_info_t* out_info) {
    out_info->model = simulated_chip;
    out_info->cores = 2;
    out_info->features = CHIP_FEATURE_WIFI_BGN;
    out_info->revision = 1;
}

TEST_CASE("GPIO Validator: ESP32 Prohibited Pins", "[gpio][host]")
{
    GpioValidator validator;
    simulated_chip = CHIP_ESP32;

    // GPIO 6 is prohibited on ESP32 (SPI Flash)
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, validator.validate((gpio_num_t)6, GpioValidator::Mode::OUTPUT));

    // GPIO 21 is valid
    TEST_ASSERT_EQUAL(ESP_OK, validator.validate((gpio_num_t)21, GpioValidator::Mode::OUTPUT));
}

TEST_CASE("GPIO Validator: ESP32S3 Prohibited Pins", "[gpio][host]")
{
    GpioValidator validator;
    simulated_chip = CHIP_ESP32S3;

    // GPIO 26 is prohibited on S3 (SPI Flash)
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, validator.validate((gpio_num_t)26, GpioValidator::Mode::OUTPUT));

    // GPIO 1 is valid
    TEST_ASSERT_EQUAL(ESP_OK, validator.validate((gpio_num_t)1, GpioValidator::Mode::OUTPUT));
}

TEST_CASE("GPIO Validator: ESP32C3 Prohibited Pins", "[gpio][host]")
{
    GpioValidator validator;
    simulated_chip = CHIP_ESP32C3;

    // GPIO 12 is prohibited on C3 (SPI Flash)
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, validator.validate((gpio_num_t)12, GpioValidator::Mode::OUTPUT));
}

void app_main(void)
{
    UNITY_BEGIN();
    unity_run_tests_by_tag("[host]", false);
    UNITY_END();
}
