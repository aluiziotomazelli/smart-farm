#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "power_control.hpp"
#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "PowerControlTest";

// ============================================================================
// GPIO DEFINITIONS PER TARGET
// ============================================================================

#if CONFIG_IDF_TARGET_ESP32
#define TEST_GPIO_LED GPIO_NUM_2     // Standard Built-in LED
#define TEST_GPIO_VALID_1 GPIO_NUM_4 // Safe GPIO
#define TEST_GPIO_VALID_2 GPIO_NUM_5 // Safe GPIO
static const gpio_num_t TEST_GPIO_FLASH_ARRAY[]      = {GPIO_NUM_6, GPIO_NUM_7,
                                                        GPIO_NUM_8, GPIO_NUM_11};
static const gpio_num_t TEST_GPIO_INPUT_ONLY_ARRAY[] = {GPIO_NUM_34, GPIO_NUM_35,
                                                        GPIO_NUM_36, GPIO_NUM_39};
#endif

#if CONFIG_IDF_TARGET_ESP32C3
#define TEST_GPIO_LED GPIO_NUM_8     // Super Mini LED
#define TEST_GPIO_VALID_1 GPIO_NUM_4 // Safe GPIO
#define TEST_GPIO_VALID_2 GPIO_NUM_5 // Safe GPIO
static const gpio_num_t TEST_GPIO_FLASH_ARRAY[] = {GPIO_NUM_12, GPIO_NUM_13,
                                                   GPIO_NUM_14, GPIO_NUM_15};
#endif

#if CONFIG_IDF_TARGET_ESP32S3
#define TEST_GPIO_LED GPIO_NUM_1     // Generic safe pin (S3 RGB is complex)
#define TEST_GPIO_VALID_1 GPIO_NUM_4 // Safe GPIO
#define TEST_GPIO_VALID_2 GPIO_NUM_5 // Safe GPIO
static const gpio_num_t TEST_GPIO_FLASH_ARRAY[] = {GPIO_NUM_26, GPIO_NUM_27,
                                                   GPIO_NUM_28, GPIO_NUM_29};
#endif

// Helper function for visual tests
static void visual_delay(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/**
 * 1. Brief Test that valid GPIO initializes correctly
 */
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

/**
 * 2. Brief Test that prohibited pins are rejected
 */
TEST_CASE("PowerControl: Init with prohibited (Flash) pins",
          "[power_control][init][negative]")
{
    for (size_t i = 0;
         i < sizeof(TEST_GPIO_FLASH_ARRAY) / sizeof(TEST_GPIO_FLASH_ARRAY[0]); i++) {
        ESP_LOGI(TAG, "Testing prohibited GPIO %d", TEST_GPIO_FLASH_ARRAY[i]);
        PowerControl::Config cfg = {.gpio           = TEST_GPIO_FLASH_ARRAY[i],
                                    .inverted_logic = false,
                                    .initial_on     = false};
        PowerControl pc(cfg);
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, pc.init());
    }
}

/**
 * 3. Brief Test with input-only pins (ESP32 only)
 */
#if CONFIG_IDF_TARGET_ESP32
TEST_CASE("PowerControl: Init with ESP32 input-only pins",
          "[power_control][init][negative]")
{
    for (size_t i = 0; i < sizeof(TEST_GPIO_INPUT_ONLY_ARRAY) /
                               sizeof(TEST_GPIO_INPUT_ONLY_ARRAY[0]);
         i++) {
        ESP_LOGI(TAG, "Testing input-only GPIO %d", TEST_GPIO_INPUT_ONLY_ARRAY[i]);
        PowerControl::Config cfg = {.gpio           = TEST_GPIO_INPUT_ONLY_ARRAY[i],
                                    .inverted_logic = false,
                                    .initial_on     = false};
        PowerControl pc(cfg);
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, pc.init());
    }
}
#endif

/**
 * 4. Brief Test that normal logic works
 */
TEST_CASE("PowerControl: Normal Logic Validation", "[power_control][logic]")
{
    const gpio_num_t PIN     = TEST_GPIO_VALID_1;
    PowerControl::Config cfg = {
        .gpio = PIN, .inverted_logic = false, .initial_on = false};
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

/**
 * 5. Brief Test that inverted logic works
 */
TEST_CASE("PowerControl: Inverted Logic Validation", "[power_control][logic]")
{
    const gpio_num_t PIN     = TEST_GPIO_VALID_2;
    PowerControl::Config cfg = {
        .gpio = PIN, .inverted_logic = true, .initial_on = false};
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

/**
 * 6. Brief Test that toggle works with inverted logic
 */
TEST_CASE("PowerControl: Toggle with inverted logic", "[power_control][logic]")
{
    PowerControl::Config cfg = {
        .gpio = TEST_GPIO_VALID_1, .inverted_logic = true, .initial_on = false};
    PowerControl pc(cfg);
    pc.init();
    bool initial = pc.isOn();
    pc.toggle();
    TEST_ASSERT_NOT_EQUAL(initial, pc.isOn());
}

/**
 * 7. Brief Test that multiple instances are handled properly
 */
TEST_CASE("PowerControl: Multiple Instances", "[power_control][multi]")
{
    PowerControl::Config cfg1 = {
        .gpio = TEST_GPIO_VALID_1, .inverted_logic = false, .initial_on = false};
    PowerControl::Config cfg2 = {
        .gpio = TEST_GPIO_VALID_2, .inverted_logic = true, .initial_on = true};

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
/**
 * 8. Stack Lifecycle Test to ensure no memory leaks with multiple instances
 */
TEST_CASE("PowerControl: Stack Lifecycle", "[power_control][memory]")
{
    size_t free_heap_before = heap_caps_get_free_size(MALLOC_CAP_8BIT);

    {
        PowerControl::Config cfg = {};
        cfg.gpio                 = TEST_GPIO_VALID_1;
        cfg.inverted_logic       = false;
        cfg.initial_on           = false;
        auto pc1                 = PowerControl(cfg);
        cfg.gpio                 = TEST_GPIO_VALID_2;
        auto pc2                 = PowerControl(cfg);
        TEST_ASSERT_EQUAL(ESP_OK, pc1.init());
        TEST_ASSERT_EQUAL(ESP_OK, pc2.init());
        TEST_ASSERT_EQUAL(ESP_OK, pc1.deinit());
        TEST_ASSERT_EQUAL(ESP_OK, pc2.deinit());
    }

    size_t free_heap_after = heap_caps_get_free_size(MALLOC_CAP_8BIT);

    // Allow small variation due to logging or other background tasks if any
    // but ideally it should be zero.
    TEST_ASSERT_UINT32_WITHIN(10, free_heap_before, free_heap_after);
}

/**
 *  9. Brief Test that deinit sets the power control to a safe state (OFF)
 */
TEST_CASE("PowerControl: Heap Lifecycle & Footprint", "[power_control][memory]")
{
    size_t free_heap_before = heap_caps_get_free_size(MALLOC_CAP_8BIT);

    // Create intentional leak scenario
    PowerControl::Config cfg = {};
    cfg.gpio                 = TEST_GPIO_VALID_1;
    cfg.inverted_logic       = false;
    cfg.initial_on           = false;
    auto *pc1                = new PowerControl(cfg);
    cfg.gpio                 = TEST_GPIO_VALID_2;
    auto *pc2                = new PowerControl(cfg);

    TEST_ASSERT_EQUAL(ESP_OK, pc1->init());
    TEST_ASSERT_EQUAL(ESP_OK, pc2->init());

    size_t heap_after_allocation = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    int memory_allocated         = free_heap_before - heap_after_allocation;

    TEST_ASSERT_GREATER_THAN(0, memory_allocated);

    TEST_ASSERT_EQUAL(ESP_OK, pc1->deinit());
    TEST_ASSERT_EQUAL(ESP_OK, pc2->deinit());

    delete pc1;
    delete pc2;

    size_t heap_after_cleanup = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    int memory_freed          = heap_after_cleanup - heap_after_allocation;

    ESP_LOGI(TAG, "Memory allocated: %d bytes", memory_allocated);
    ESP_LOGI(TAG, "Memory cleaned up: %d bytes", memory_freed);

    // Allow small variation for logging, fragmentation, etc.
    TEST_ASSERT_UINT32_WITHIN(10, free_heap_before, heap_after_cleanup);

    int net_leak = memory_allocated - memory_freed;
    ESP_LOGI(TAG, "Net leak: %d bytes", net_leak);
    // Freed memory should approximately equal allocated memory
    TEST_ASSERT_UINT32_WITHIN(10, memory_allocated, memory_freed);
}

/**
 *  10. Brief Test that deinit sets the power control to a safe state (OFF)
 */
TEST_CASE("PowerControl: Deinit sets safe state", "[power_control][state]")
{
    const gpio_num_t PIN     = TEST_GPIO_VALID_1;
    PowerControl::Config cfg = {
        .gpio = PIN, .inverted_logic = false, .initial_on = true};
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

/**
 * 11. Brief Test that state is consistent
 */
TEST_CASE("PowerControl: State consistency", "[power_control][state]")
{
    PowerControl::Config cfg = {
        .gpio = TEST_GPIO_VALID_1, .inverted_logic = false, .initial_on = false};
    PowerControl pc(cfg);
    pc.init();
    pc.turnOn();
    // Force state change and verify consistency
    TEST_ASSERT_EQUAL(pc.isOn(), gpio_get_level(cfg.gpio) ^ cfg.inverted_logic);
}

/**
 * 12. Brief Test that re-initialization is handled properly
 */
TEST_CASE("PowerControl: Re-initialization", "[power_control][lifecycle]")
{
    PowerControl::Config cfg = {
        .gpio = TEST_GPIO_VALID_1, .inverted_logic = false, .initial_on = false};
    PowerControl pc(cfg);
    pc.init();
    pc.deinit();
    TEST_ASSERT_EQUAL(ESP_OK, pc.init()); // Re-init após deinit
}

/**
 * 13. Brief Test that methods are rejected before initialization
 */
TEST_CASE("PowerControl: Methods before init", "[power_control][negative]")
{
    PowerControl::Config cfg = {
        .gpio = TEST_GPIO_VALID_1, .inverted_logic = false, .initial_on = false};
    PowerControl pc(cfg);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, pc.turnOn());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, pc.turnOff());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, pc.toggle());
}

/**
 * 14. Brief Test that visual blink works
 */
TEST_CASE("PowerControl: Visual Blink", "[power_control][visual]")
{
    ESP_LOGI(TAG, "Starting visual blink test on GPIO %d", TEST_GPIO_LED);
    PowerControl::Config cfg = {
        .gpio = TEST_GPIO_LED, .inverted_logic = false, .initial_on = false};
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
