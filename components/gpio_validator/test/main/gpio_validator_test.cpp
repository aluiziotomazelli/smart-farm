// gpio_validator_test.cpp
#include "esp_chip_info.h"
#include "esp_log.h"
#include "gpio_validator.hpp"
#include "unity.h"
#include "unity_test_runner.h"

static const char *TAG = "GpioValidatorTest";

// =======================================================
// Common Tests
// =======================================================

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

TEST_CASE("GpioValidator: Edge cases", "[gpio_validator][edge]")
{
    ESP_LOGI(TAG, "Testing edge cases");

    // Invalid GPIO numbers
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        GpioValidator::validate(GPIO_NUM_MAX, GpioValidator::Mode::OUTPUT));

    // Negative GPIO (if gpio_num_t is signed)
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      GpioValidator::validate(static_cast<gpio_num_t>(-1),
                                              GpioValidator::Mode::OUTPUT));

    // Very high GPIO number
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      GpioValidator::validate(static_cast<gpio_num_t>(255),
                                              GpioValidator::Mode::OUTPUT));
}

TEST_CASE("GpioValidator: Cross-chip compatibility",
          "[gpio_validator][compatibility]")
{
    ESP_LOGI(TAG, "Testing cross-chip compatibility");

    // GPIOs that should be safe on ALL ESP chips
    gpio_num_t universal_safe_gpios[] = {
        GPIO_NUM_2,  // Usually LED
        GPIO_NUM_4,  // Commonly available
        GPIO_NUM_5,  // Commonly available
        GPIO_NUM_18, // Usually available
        GPIO_NUM_19  // Usually available
    };

    for (auto gpio : universal_safe_gpios) {
        ESP_LOGI(TAG, "Testing universal safe GPIO %d", gpio);

        // Should work on any chip
        TEST_ASSERT_EQUAL(
            ESP_OK, GpioValidator::validate(gpio, GpioValidator::Mode::OUTPUT));
    }
}

// =======================================================
// ESP32 Tests
// =======================================================

#if CONFIG_IDF_TARGET_ESP32

TEST_CASE("GpioValidator: ESP32 Flash pins rejection",
          "[gpio_validator][esp32][critical]")
{
    ESP_LOGI(TAG, "Testing ESP32 flash pins (must be rejected)");

    // ESP32 flash pins that cause system reset
    gpio_num_t flash_pins[] = {GPIO_NUM_6,  GPIO_NUM_7,  GPIO_NUM_8,  GPIO_NUM_9,
                               GPIO_NUM_10, GPIO_NUM_11, GPIO_NUM_16, GPIO_NUM_17};

    for (auto gpio : flash_pins) {
        ESP_LOGI(TAG, "Testing flash pin GPIO %d (should be rejected)", gpio);

        // Should fail for OUTPUT
        TEST_ASSERT_EQUAL(
            ESP_ERR_INVALID_ARG,
            GpioValidator::validate(gpio, GpioValidator::Mode::OUTPUT));

        // Should also fail for INPUT (still dangerous)
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                          GpioValidator::validate(gpio, GpioValidator::Mode::INPUT));
    }
}

TEST_CASE("GpioValidator: ESP32 Input-only pins",
          "[gpio_validator][esp32][input_only]")
{
    ESP_LOGI(TAG, "Testing ESP32 input-only pins");

    // ESP32 input-only pins (34-39)
    gpio_num_t input_only_pins[] = {GPIO_NUM_34, GPIO_NUM_35, GPIO_NUM_36,
                                    GPIO_NUM_37, GPIO_NUM_38, GPIO_NUM_39};

    for (auto gpio : input_only_pins) {
        ESP_LOGI(TAG, "Testing input-only GPIO %d", gpio);

        // Should fail for OUTPUT
        TEST_ASSERT_EQUAL(
            ESP_ERR_INVALID_ARG,
            GpioValidator::validate(gpio, GpioValidator::Mode::OUTPUT));

        // Should PASS for INPUT
        TEST_ASSERT_EQUAL(ESP_OK,
                          GpioValidator::validate(gpio, GpioValidator::Mode::INPUT));
    }
}

TEST_CASE("GpioValidator: ESP32 Warning pins", "[gpio_validator][esp32][warning]")
{
    ESP_LOGI(TAG, "Testing ESP32 warning pins (should pass with warnings)");

    // These should pass validation but show warnings
    struct
    {
        gpio_num_t gpio;
        const char *expected_function;
    } warning_pins[] = {
        {GPIO_NUM_0, "strapping"},      {GPIO_NUM_2, "strapping/LED"},
        {GPIO_NUM_5, "strapping"},      {GPIO_NUM_1, "UART0_TX"},
        {GPIO_NUM_3, "UART0_RX"},       {GPIO_NUM_12, "JTAG/strapping"},
        {GPIO_NUM_15, "JTAG/strapping"}};

    for (auto &test : warning_pins) {
        ESP_LOGI(TAG, "Testing warning pin GPIO %d (%s)", test.gpio,
                 test.expected_function);

        // Should PASS validation (warnings are logged, not errors)
        TEST_ASSERT_EQUAL(
            ESP_OK, GpioValidator::validate(test.gpio, GpioValidator::Mode::OUTPUT));
        TEST_ASSERT_EQUAL(
            ESP_OK, GpioValidator::validate(test.gpio, GpioValidator::Mode::INPUT));
    }
}
#endif // CONFIG_IDF_TARGET_ESP32

// =======================================================
// ESP32-C3 Tests
// =======================================================
#if CONFIG_IDF_TARGET_ESP32C3

TEST_CASE("GpioValidator: ESP32C3 Flash pins rejection",
          "[gpio_validator][esp32c3][critical]")
{
    ESP_LOGI(TAG, "Testing ESP32-C3 flash pins (must be rejected)");

    // ESP32-C3 flash pins (12-17) - should be rejected
    gpio_num_t c3_flash_pins[] = {GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_14,
                                  GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_17};

    for (auto gpio : c3_flash_pins) {
        ESP_LOGI(TAG, "Testing flash pin GPIO %d , (should be rejected)", gpio);

        // Should fail for OUTPUT
        TEST_ASSERT_EQUAL(
            ESP_ERR_INVALID_ARG,
            GpioValidator::validate(gpio, GpioValidator::Mode::OUTPUT));

        // Should also fail for INPUT (still dangerous)
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                          GpioValidator::validate(gpio, GpioValidator::Mode::INPUT));
    }
}

TEST_CASE("GpioValidator: ESP32-C3 warning pins",
          "[gpio_validator][esp32c3][warning]")
{
    ESP_LOGI(TAG, "Testing ESP32-C3 warning pins (should pass with warnings)");

    // These should pass validation but show warnings
    struct
    {
        gpio_num_t gpio;
        const char *expected_function;
    } warning_pins[] = {{GPIO_NUM_2, "strapping"},
                        {GPIO_NUM_8, "strapping"},
                        {GPIO_NUM_9, "strapping"},

                        {GPIO_NUM_18, "JTAG/strapping"},
                        {GPIO_NUM_19, "JTAG/strapping"}};

    for (auto &test : warning_pins) {
        ESP_LOGI(TAG, "Testing warning pin GPIO %d (%s)", test.gpio,
                 test.expected_function);

        // Should PASS validation (warnings are logged, not errors)
        TEST_ASSERT_EQUAL(
            ESP_OK, GpioValidator::validate(test.gpio, GpioValidator::Mode::OUTPUT));
        TEST_ASSERT_EQUAL(
            ESP_OK, GpioValidator::validate(test.gpio, GpioValidator::Mode::INPUT));
    }
}
#endif // CONFIG_IDF_TARGET_ESP32C3

// =======================================================
// ESP32-S3 Tests
// =======================================================
#if CONFIG_IDF_TARGET_ESP32S3

TEST_CASE("GpioValidator: ESP32S3 Flash pins rejection",
          "[gpio_validator][esp32s3][critical]")
{
    ESP_LOGI(TAG, "Testing ESP32-S3 flash pins (must be rejected)");

    // ESP32-S3 flash pins (26-32) - should be rejected
    gpio_num_t s3_flash_pins[] = {GPIO_NUM_26, GPIO_NUM_27, GPIO_NUM_28,
                                  GPIO_NUM_29, GPIO_NUM_30, GPIO_NUM_31,
                                  GPIO_NUM_32, GPIO_NUM_33, GPIO_NUM_34,
                                  GPIO_NUM_35, GPIO_NUM_36, GPIO_NUM_37};

    for (auto gpio : s3_flash_pins) {
        ESP_LOGI(TAG, "Testing flash pin GPIO %d , (should be rejected)", gpio);

        // Should fail for OUTPUT
        TEST_ASSERT_EQUAL(
            ESP_ERR_INVALID_ARG,
            GpioValidator::validate(gpio, GpioValidator::Mode::OUTPUT));

        // Should also fail for INPUT (still dangerous)
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                          GpioValidator::validate(gpio, GpioValidator::Mode::INPUT));
    }
}

TEST_CASE("GpioValidator: ESP32-S3 warning pins",
          "[gpio_validator][esp32s3][warning]")
{
    ESP_LOGI(TAG, "Testing ESP32-S3 warning pins (should pass with warnings)");

    // These should pass validation but show warnings
    struct
    {
        gpio_num_t gpio;
        const char *expected_function;
    } warning_pins[] = {{GPIO_NUM_0, "strapping"},  {GPIO_NUM_3, "strapping"},
                        {GPIO_NUM_45, "strapping"}, {GPIO_NUM_46, "strapping"},
                        {GPIO_NUM_19, "JTAG"},      {GPIO_NUM_20, "JTAG"}};

    for (auto &test : warning_pins) {
        ESP_LOGI(TAG, "Testing warning pin GPIO %d (%s)", test.gpio,
                 test.expected_function);

        // Should PASS validation (warnings are logged, not errors)
        TEST_ASSERT_EQUAL(
            ESP_OK, GpioValidator::validate(test.gpio, GpioValidator::Mode::OUTPUT));
        TEST_ASSERT_EQUAL(
            ESP_OK, GpioValidator::validate(test.gpio, GpioValidator::Mode::INPUT));
    }
}
#endif // CONFIG_IDF_TARGET_ESP32S3

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "  GpioValidator Component Unit Tests");
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Running tests on hardware (no mocks)");
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "Detected chip model: %d", chip_info.model);
    ESP_LOGI(TAG, "GPIO_NUM_MAX: %d", GPIO_NUM_MAX);
    ESP_LOGI(TAG, "");

    // Run tests
    UNITY_BEGIN();

    unity_run_tests_by_tag("[gpio_validator][basic]", false);
    unity_run_tests_by_tag("[gpio_validator][edge]", false);
    unity_run_tests_by_tag("[gpio_validator][compatibility]", false);

    switch (chip_info.model) {
    case CHIP_ESP32:
        ESP_LOGI(TAG, "Running ESP32-specific tests");
        unity_run_tests_by_tag("[gpio_validator][esp32]", false);
        break;

    case CHIP_ESP32C3:
        ESP_LOGI(TAG, "Running ESP32-C3-specific tests");
        unity_run_tests_by_tag("[gpio_validator][esp32c3]", false);
        break;

    case CHIP_ESP32S3:
        ESP_LOGI(TAG, "Running ESP32-S3-specific tests");
        unity_run_tests_by_tag("[gpio_validator][esp32s3]", false);
        break;

    default:
        ESP_LOGW(TAG, "Chip model %d - running generic tests only", chip_info.model);
        break;
    }

    UNITY_END();

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "  All tests completed!");
    ESP_LOGI(TAG, "===========================================");
}