// test_gpio_validator.cpp
#include "esp_log.h"
#include "gpio_validator.hpp"
#include "unity.h"
#include "unity_test_runner.h"

static const char *TAG = "GpioValidatorTest";

// ============================================================================
// TARGET SPECIFIC PIN MAPS
// ============================================================================

static const gpio_num_t VALID_GPIOS[] = {GPIO_NUM_4}; // GPIOs valid across targets
#if CONFIG_IDF_TARGET_ESP32
static const gpio_num_t FLASH_PINS[]      = {GPIO_NUM_6,  GPIO_NUM_7,  GPIO_NUM_8,
                                             GPIO_NUM_9,  GPIO_NUM_10, GPIO_NUM_11,
                                             GPIO_NUM_16, GPIO_NUM_17};
static const gpio_num_t INPUT_ONLY_PINS[] = {GPIO_NUM_34, GPIO_NUM_35, GPIO_NUM_36,
                                             GPIO_NUM_37, GPIO_NUM_38, GPIO_NUM_39};
static const gpio_num_t WARNING_PINS[]    = {GPIO_NUM_0, GPIO_NUM_1, GPIO_NUM_2,
                                             GPIO_NUM_3, GPIO_NUM_5, GPIO_NUM_12,
                                             GPIO_NUM_15};
#elif CONFIG_IDF_TARGET_ESP32S3
static const gpio_num_t FLASH_PINS[] = {
    GPIO_NUM_26, GPIO_NUM_27, GPIO_NUM_28, GPIO_NUM_29, GPIO_NUM_30, GPIO_NUM_31,
    GPIO_NUM_32, GPIO_NUM_33, GPIO_NUM_34, GPIO_NUM_35, GPIO_NUM_36, GPIO_NUM_37};
static const gpio_num_t INPUT_ONLY_PINS[] = {}; // Not aplicable for ESP32-S3
static const gpio_num_t WARNING_PINS[]    = {GPIO_NUM_0,  GPIO_NUM_3,  GPIO_NUM_19,
                                             GPIO_NUM_20, GPIO_NUM_43, GPIO_NUM_44,
                                             GPIO_NUM_45, GPIO_NUM_46};
#elif CONFIG_IDF_TARGET_ESP32C3
static const gpio_num_t FLASH_PINS[]      = {GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_14,
                                             GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_17};
static const gpio_num_t INPUT_ONLY_PINS[] = {}; // Not aplicable for ESP32-C3
static const gpio_num_t WARNING_PINS[]    = {GPIO_NUM_2, GPIO_NUM_8, GPIO_NUM_9,
                                             GPIO_NUM_18, GPIO_NUM_19};
#else
static const gpio_num_t FLASH_PINS[]      = {};
static const gpio_num_t INPUT_ONLY_PINS[] = {};
static const gpio_num_t WARNING_PINS[]    = {};
#endif

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

// ============================================================================
// GENERIC TESTS (Work for all targets)
// ============================================================================

TEST_CASE("GpioValidator: Basic valid GPIOs", "[gpio_validator][basic]")
{
    for (size_t i = 0; i < ARRAY_SIZE(VALID_GPIOS); i++) {
        gpio_num_t pin = VALID_GPIOS[i];
        ESP_LOGI(TAG, "Testing valid GPIO %d", pin);
        TEST_ASSERT_EQUAL(ESP_OK,
                          GpioValidator::validate(pin, GpioValidator::Mode::OUTPUT));
        TEST_ASSERT_EQUAL(ESP_OK,
                          GpioValidator::validate(pin, GpioValidator::Mode::INPUT));
    }
}

TEST_CASE("GpioValidator: Flash pins rejection", "[gpio_validator][critical]")
{
    for (size_t i = 0; i < ARRAY_SIZE(FLASH_PINS); i++) {
        gpio_num_t pin = FLASH_PINS[i];
        ESP_LOGI(TAG, "Testing flash pin GPIO %d", pin);
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                          GpioValidator::validate(pin, GpioValidator::Mode::OUTPUT));
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                          GpioValidator::validate(pin, GpioValidator::Mode::INPUT));
    }
}

TEST_CASE("GpioValidator: ESP32 Input-only pins (must warning)",
          "[gpio_validator][input_only]")
{
    if (ARRAY_SIZE(INPUT_ONLY_PINS) == 0) {
        ESP_LOGI(TAG, "No input-only pins for this target. Skipping.");
        return;
    }

    for (size_t i = 0; i < ARRAY_SIZE(INPUT_ONLY_PINS); i++) {
        gpio_num_t pin = INPUT_ONLY_PINS[i];
        // Must FAIL for OUTPUT and PASS for INPUT
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                          GpioValidator::validate(pin, GpioValidator::Mode::OUTPUT));
        TEST_ASSERT_EQUAL(ESP_OK,
                          GpioValidator::validate(pin, GpioValidator::Mode::INPUT));
    }
}

TEST_CASE("GpioValidator: Warning pins (should pass)", "[gpio_validator][warning]")
{
    for (size_t i = 0; i < ARRAY_SIZE(WARNING_PINS); i++) {
        gpio_num_t pin = WARNING_PINS[i];
        ESP_LOGI(TAG, "Testing warning/strapping pin GPIO %d", pin);
        TEST_ASSERT_EQUAL(ESP_OK,
                          GpioValidator::validate(pin, GpioValidator::Mode::OUTPUT));
    }
}

TEST_CASE("GpioValidator: Warning pins with INPUT mode", "[gpio_validator][warning]")
{
    for (size_t i = 0; i < ARRAY_SIZE(WARNING_PINS); i++) {
        gpio_num_t pin = WARNING_PINS[i];
        TEST_ASSERT_EQUAL(ESP_OK,
                          GpioValidator::validate(pin, GpioValidator::Mode::INPUT));
    }
}

TEST_CASE("GpioValidator: Edge cases", "[gpio_validator][edge]")
{
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        GpioValidator::validate(GPIO_NUM_MAX, GpioValidator::Mode::OUTPUT));
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        GpioValidator::validate((gpio_num_t)-1, GpioValidator::Mode::OUTPUT));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      GpioValidator::validate(static_cast<gpio_num_t>(100),
                                              GpioValidator::Mode::OUTPUT));
}
