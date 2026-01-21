#include "esp_log.h"
#include "power_control.hpp"
#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_heap_caps.h"

static const char *TAG = "PowerControlTest";

// ============================================================================
// GPIO DEFINITIONS PER TARGET
// ============================================================================

#if CONFIG_IDF_TARGET_ESP32
#define TEST_GPIO_LED GPIO_NUM_2     // Standard Built-in LED
#define TEST_GPIO_VALID_1 GPIO_NUM_4 // Safe GPIO
#define TEST_GPIO_VALID_2 GPIO_NUM_5 // Safe GPIO
static const gpio_num_t TEST_GPIO_FLASH_ARRAY[] = {GPIO_NUM_6, GPIO_NUM_7, GPIO_NUM_8, GPIO_NUM_11};
static const gpio_num_t TEST_GPIO_INPUT_ONLY_ARRAY[] = {GPIO_NUM_34, GPIO_NUM_35, GPIO_NUM_36, GPIO_NUM_39};
#endif

#if CONFIG_IDF_TARGET_ESP32C3
#define TEST_GPIO_LED GPIO_NUM_8     // Super Mini LED
#define TEST_GPIO_VALID_1 GPIO_NUM_4 // Safe GPIO
#define TEST_GPIO_VALID_2 GPIO_NUM_5 // Safe GPIO
static const gpio_num_t TEST_GPIO_FLASH_ARRAY[] = {GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_14, GPIO_NUM_15};
#endif

#if CONFIG_IDF_TARGET_ESP32S3
#define TEST_GPIO_LED GPIO_NUM_1     // Generic safe pin (S3 RGB is complex)
#define TEST_GPIO_VALID_1 GPIO_NUM_4 // Safe GPIO
#define TEST_GPIO_VALID_2 GPIO_NUM_5 // Safe GPIO
static const gpio_num_t TEST_GPIO_FLASH_ARRAY[] = {GPIO_NUM_26, GPIO_NUM_27, GPIO_NUM_28, GPIO_NUM_29};
#endif

// Helper function for visual tests
static void visual_delay(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

// ============================================================================
// 1. INITIALIZATION TESTS
// ============================================================================

TEST_CASE("PowerControl: Init with valid GPIO", "[power_control][init]")
{
    PowerControl::Config cfg = {
        .gpio = TEST_GPIO_VALID_1, .inverted_logic = false, .initial_on = false};

    PowerControl pc(cfg);
    TEST_ASSERT_FALSE(pc.isInitialized());
    TEST_ASSERT_EQUAL(ESP_OK, pc.init());
    TEST_ASSERT_TRUE(pc.isInitialized());
    TEST_ASSERT_EQUAL(TEST_GPIO_VALID_1, pc.getPin());
    TEST_ASSERT_EQUAL(ESP_OK, pc.deinit());
}

TEST_CASE("PowerControl: Init with prohibited (Flash) pins", "[power_control][init][negative]")
{
    for (size_t i = 0; i < sizeof(TEST_GPIO_FLASH_ARRAY) / sizeof(TEST_GPIO_FLASH_ARRAY[0]); i++) {
        ESP_LOGI(TAG, "Testing prohibited GPIO %d", TEST_GPIO_FLASH_ARRAY[i]);
        PowerControl::Config cfg = {.gpio = TEST_GPIO_FLASH_ARRAY[i], .inverted_logic = false, .initial_on = false};
        PowerControl pc(cfg);
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, pc.init());
    }
}

#if CONFIG_IDF_TARGET_ESP32
TEST_CASE("PowerControl: Init with ESP32 input-only pins", "[power_control][init][negative]")
{
    for (size_t i = 0; i < sizeof(TEST_GPIO_INPUT_ONLY_ARRAY) / sizeof(TEST_GPIO_INPUT_ONLY_ARRAY[0]); i++) {
        ESP_LOGI(TAG, "Testing input-only GPIO %d", TEST_GPIO_INPUT_ONLY_ARRAY[i]);
        PowerControl::Config cfg = {.gpio = TEST_GPIO_INPUT_ONLY_ARRAY[i], .inverted_logic = false, .initial_on = false};
        PowerControl pc(cfg);
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, pc.init());
    }
}
#endif

// ============================================================================
// 2. AUTOMATED LOGIC TESTS (Fast)
// ============================================================================

TEST_CASE("PowerControl: Normal Logic Validation", "[power_control][logic]")
{
    const gpio_num_t PIN = TEST_GPIO_VALID_1;
    PowerControl::Config cfg = {.gpio = PIN, .inverted_logic = false, .initial_on = false};
    PowerControl pc(cfg);

    TEST_ASSERT_EQUAL(ESP_OK, pc.init());

    // Test OFF
    TEST_ASSERT_EQUAL(ESP_OK, pc.turnOff());
    TEST_ASSERT_FALSE(pc.isOn());
    TEST_ASSERT_EQUAL(0, gpio_get_level(PIN));

    // Test ON
    TEST_ASSERT_EQUAL(ESP_OK, pc.turnOn());
    TEST_ASSERT_TRUE(pc.isOn());
    TEST_ASSERT_EQUAL(1, gpio_get_level(PIN));

    // Test Toggle
    TEST_ASSERT_EQUAL(ESP_OK, pc.toggle());
    TEST_ASSERT_FALSE(pc.isOn());
    TEST_ASSERT_EQUAL(0, gpio_get_level(PIN));

    TEST_ASSERT_EQUAL(ESP_OK, pc.deinit());
}

TEST_CASE("PowerControl: Inverted Logic Validation", "[power_control][logic]")
{
    const gpio_num_t PIN = TEST_GPIO_VALID_2;
    PowerControl::Config cfg = {.gpio = PIN, .inverted_logic = true, .initial_on = false};
    PowerControl pc(cfg);

    TEST_ASSERT_EQUAL(ESP_OK, pc.init());

    // Test OFF (Inverted -> Physical HIGH)
    TEST_ASSERT_EQUAL(ESP_OK, pc.turnOff());
    TEST_ASSERT_FALSE(pc.isOn());
    TEST_ASSERT_EQUAL(1, gpio_get_level(PIN));

    // Test ON (Inverted -> Physical LOW)
    TEST_ASSERT_EQUAL(ESP_OK, pc.turnOn());
    TEST_ASSERT_TRUE(pc.isOn());
    TEST_ASSERT_EQUAL(0, gpio_get_level(PIN));

    TEST_ASSERT_EQUAL(ESP_OK, pc.deinit());
}

// ============================================================================
// 3. MULTIPLE INSTANCES & MEMORY TESTS
// ============================================================================

TEST_CASE("PowerControl: Multiple Instances", "[power_control][multi]")
{
    PowerControl::Config cfg1 = {.gpio = TEST_GPIO_VALID_1, .inverted_logic = false, .initial_on = false};
    PowerControl::Config cfg2 = {.gpio = TEST_GPIO_VALID_2, .inverted_logic = true, .initial_on = true};

    PowerControl pc1(cfg1);
    PowerControl pc2(cfg2);

    TEST_ASSERT_EQUAL(ESP_OK, pc1.init());
    TEST_ASSERT_EQUAL(ESP_OK, pc2.init());

    TEST_ASSERT_FALSE(pc1.isOn());
    TEST_ASSERT_TRUE(pc2.isOn());

    // Physical check
    TEST_ASSERT_EQUAL(0, gpio_get_level(TEST_GPIO_VALID_1)); // Normal OFF
    TEST_ASSERT_EQUAL(0, gpio_get_level(TEST_GPIO_VALID_2)); // Inverted ON

    pc1.turnOn();
    pc2.turnOff();

    TEST_ASSERT_EQUAL(1, gpio_get_level(TEST_GPIO_VALID_1)); // Normal ON
    TEST_ASSERT_EQUAL(1, gpio_get_level(TEST_GPIO_VALID_2)); // Inverted OFF

    TEST_ASSERT_EQUAL(ESP_OK, pc1.deinit());
    TEST_ASSERT_EQUAL(ESP_OK, pc2.deinit());
}

TEST_CASE("PowerControl: Memory Leak Check", "[power_control][memory]")
{
    size_t free_heap_before = heap_caps_get_free_size(MALLOC_CAP_8BIT);

    {
        PowerControl::Config cfg = {.gpio = TEST_GPIO_VALID_1, .inverted_logic = false, .initial_on = false};
        auto *pc = new PowerControl(cfg);
        TEST_ASSERT_EQUAL(ESP_OK, pc->init());
        TEST_ASSERT_EQUAL(ESP_OK, pc->deinit());
        delete pc;
    }

    size_t free_heap_after = heap_caps_get_free_size(MALLOC_CAP_8BIT);

    // Allow small variation due to logging or other background tasks if any
    // but ideally it should be zero.
    TEST_ASSERT_UINT32_WITHIN(100, free_heap_before, free_heap_after);
}

// ============================================================================
// 4. STATE & DEINIT TESTS
// ============================================================================

TEST_CASE("PowerControl: Deinit sets safe state", "[power_control][state]")
{
    const gpio_num_t PIN = TEST_GPIO_VALID_1;
    PowerControl::Config cfg = {.gpio = PIN, .inverted_logic = false, .initial_on = true};
    PowerControl pc(cfg);

    TEST_ASSERT_EQUAL(ESP_OK, pc.init());
    TEST_ASSERT_TRUE(pc.isOn());
    TEST_ASSERT_EQUAL(1, gpio_get_level(PIN));

    TEST_ASSERT_EQUAL(ESP_OK, pc.deinit());
    TEST_ASSERT_FALSE(pc.isInitialized());
    TEST_ASSERT_FALSE(pc.isOn());

    // Deinit should set level to 0 as per implementation
    TEST_ASSERT_EQUAL(0, gpio_get_level(PIN));
}

// ============================================================================
// 5. VISUAL TESTS (Manual inspection)
// ============================================================================

TEST_CASE("PowerControl: Visual Blink", "[power_control][visual]")
{
    ESP_LOGI(TAG, "Starting visual blink test on GPIO %d", TEST_GPIO_LED);
    PowerControl::Config cfg = {.gpio = TEST_GPIO_LED, .inverted_logic = false, .initial_on = false};
    PowerControl led(cfg);

    if (led.init() != ESP_OK) {
        ESP_LOGW(TAG, "LED pin not available for visual test, skipping.");
        return;
    }

    for (int i = 0; i < 3; i++) {
        led.turnOn();
        visual_delay(200);
        led.turnOff();
        visual_delay(200);
    }

    led.deinit();
}
