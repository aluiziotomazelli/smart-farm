#include "esp_log.h"
#include "power_control.hpp"
#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "PowerControlTest";

// GPIO definitions for testing
#define TEST_GPIO_LED GPIO_NUM_2     // Built-in LED
#define TEST_GPIO_VALID_1 GPIO_NUM_4 // Valid output GPIO
#define TEST_GPIO_VALID_2 GPIO_NUM_5 // Valid output GPIO

// Invalid GPIOs - Flash pins
#define TEST_GPIO_FLASH_1 GPIO_NUM_6
#define TEST_GPIO_FLASH_2 GPIO_NUM_7
#define TEST_GPIO_FLASH_3 GPIO_NUM_8
#define TEST_GPIO_FLASH_4 GPIO_NUM_11

// Invalid GPIOs - Input-only pins
#define TEST_GPIO_INPUT_ONLY_1 GPIO_NUM_34
#define TEST_GPIO_INPUT_ONLY_2 GPIO_NUM_35
#define TEST_GPIO_INPUT_ONLY_3 GPIO_NUM_36
#define TEST_GPIO_INPUT_ONLY_4 GPIO_NUM_39

// Helper function to add delay for visual inspection
static void visual_delay(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

// ============================================================================
// 1. INITIALIZATION TESTS
// ============================================================================

TEST_CASE("PowerControl: Init with valid GPIO", "[power_control][init]")
{
    ESP_LOGI(TAG, "Testing initialization with valid GPIO (GPIO 2 - LED)");

    PowerControl::Config cfg = {
        .gpio = TEST_GPIO_LED, .inverted_logic = false, .initial_on = false};

    PowerControl pc(cfg);

    // Should not be initialized before init()
    TEST_ASSERT_FALSE(pc.isInitialized());

    // Init should succeed
    TEST_ASSERT_EQUAL(ESP_OK, pc.init());

    // Should be initialized after init()
    TEST_ASSERT_TRUE(pc.isInitialized());

    // Pin should match configuration
    TEST_ASSERT_EQUAL(TEST_GPIO_LED, pc.getPin());

    // Cleanup
    TEST_ASSERT_EQUAL(ESP_OK, pc.deinit());
}

// TEST_CASE("PowerControl: Init with invalid GPIO - Flash pins",
//           "[power_control][init][negative]")
// {
//     ESP_LOGI(TAG, "Testing initialization with flash pins (should fail)");

//     gpio_num_t invalid_gpios[] = {TEST_GPIO_FLASH_1, TEST_GPIO_FLASH_2,
//                                   TEST_GPIO_FLASH_3, TEST_GPIO_FLASH_4};

//     for (int i = 0; i < sizeof(invalid_gpios) / sizeof(invalid_gpios[0]); i++) {
//         ESP_LOGI(TAG, "Testing GPIO %d (flash pin)", invalid_gpios[i]);

//         PowerControl::Config cfg = {
//             .gpio = invalid_gpios[i], .inverted_logic = false, .initial_on =
//             false};

//         PowerControl pc(cfg);

//         // Init should fail with invalid argument
//         TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, pc.init());

//         // Should not be initialized
//         TEST_ASSERT_FALSE(pc.isInitialized());
//     }
// }

TEST_CASE("PowerControl: Init with invalid GPIO - Input-only pins",
          "[power_control][init][negative]")
{
    ESP_LOGI(TAG, "Testing initialization with input-only pins (should fail)");

    gpio_num_t input_only_gpios[] = {TEST_GPIO_INPUT_ONLY_1, TEST_GPIO_INPUT_ONLY_2,
                                     TEST_GPIO_INPUT_ONLY_3, TEST_GPIO_INPUT_ONLY_4};

    for (int i = 0; i < sizeof(input_only_gpios) / sizeof(input_only_gpios[0]);
         i++) {
        ESP_LOGI(TAG, "Testing GPIO %d (input-only pin)", input_only_gpios[i]);

        PowerControl::Config cfg = {.gpio           = input_only_gpios[i],
                                    .inverted_logic = false,
                                    .initial_on     = false};

        PowerControl pc(cfg);

        // Init should fail with invalid argument
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, pc.init());

        // Should not be initialized
        TEST_ASSERT_FALSE(pc.isInitialized());
    }
}

TEST_CASE("PowerControl: Init with initial state ON", "[power_control][init]")
{
    ESP_LOGI(TAG, "Testing initialization with initial_on = true");

    PowerControl::Config cfg = {
        .gpio = TEST_GPIO_LED, .inverted_logic = false, .initial_on = true};

    PowerControl pc(cfg);

    TEST_ASSERT_EQUAL(ESP_OK, pc.init());

    // Should be ON after init
    TEST_ASSERT_TRUE(pc.isOn());

    ESP_LOGI(TAG, "LED should be ON now - visual check");
    visual_delay(1000);

    // Cleanup
    TEST_ASSERT_EQUAL(ESP_OK, pc.deinit());
}

TEST_CASE("PowerControl: Init with initial state OFF", "[power_control][init]")
{
    ESP_LOGI(TAG, "Testing initialization with initial_on = false");

    PowerControl::Config cfg = {
        .gpio = TEST_GPIO_LED, .inverted_logic = false, .initial_on = false};

    PowerControl pc(cfg);

    TEST_ASSERT_EQUAL(ESP_OK, pc.init());

    // Should be OFF after init
    TEST_ASSERT_FALSE(pc.isOn());

    ESP_LOGI(TAG, "LED should be OFF now - visual check");
    visual_delay(1000);

    // Cleanup
    TEST_ASSERT_EQUAL(ESP_OK, pc.deinit());
}

// ============================================================================
// 2. CONTROL TESTS (using GPIO 2 - LED)
// ============================================================================

TEST_CASE("PowerControl: Turn ON and OFF", "[power_control][control]")
{
    ESP_LOGI(TAG, "Testing turnOn() and turnOff()");

    PowerControl::Config cfg = {
        .gpio = TEST_GPIO_LED, .inverted_logic = false, .initial_on = false};

    PowerControl pc(cfg);
    TEST_ASSERT_EQUAL(ESP_OK, pc.init());

    // Turn ON
    ESP_LOGI(TAG, "Turning LED ON");
    TEST_ASSERT_EQUAL(ESP_OK, pc.turnOn());
    TEST_ASSERT_TRUE(pc.isOn());
    visual_delay(1000);

    // Turn OFF
    ESP_LOGI(TAG, "Turning LED OFF");
    TEST_ASSERT_EQUAL(ESP_OK, pc.turnOff());
    TEST_ASSERT_FALSE(pc.isOn());
    visual_delay(1000);

    // Cleanup
    TEST_ASSERT_EQUAL(ESP_OK, pc.deinit());
}

TEST_CASE("PowerControl: Toggle functionality", "[power_control][control]")
{
    ESP_LOGI(TAG, "Testing toggle()");

    PowerControl::Config cfg = {
        .gpio = TEST_GPIO_LED, .inverted_logic = false, .initial_on = false};

    PowerControl pc(cfg);
    TEST_ASSERT_EQUAL(ESP_OK, pc.init());

    // Initial state is OFF
    TEST_ASSERT_FALSE(pc.isOn());

    // Toggle to ON
    ESP_LOGI(TAG, "Toggle 1: OFF -> ON");
    TEST_ASSERT_EQUAL(ESP_OK, pc.toggle());
    TEST_ASSERT_TRUE(pc.isOn());
    visual_delay(500);

    // Toggle to OFF
    ESP_LOGI(TAG, "Toggle 2: ON -> OFF");
    TEST_ASSERT_EQUAL(ESP_OK, pc.toggle());
    TEST_ASSERT_FALSE(pc.isOn());
    visual_delay(500);

    // Toggle to ON again
    ESP_LOGI(TAG, "Toggle 3: OFF -> ON");
    TEST_ASSERT_EQUAL(ESP_OK, pc.toggle());
    TEST_ASSERT_TRUE(pc.isOn());
    visual_delay(500);

    // Cleanup
    TEST_ASSERT_EQUAL(ESP_OK, pc.deinit());
}

TEST_CASE("PowerControl: Inverted logic", "[power_control][control]")
{
    ESP_LOGI(TAG, "Testing inverted logic (active LOW)");

    PowerControl::Config cfg = {.gpio           = TEST_GPIO_LED,
                                .inverted_logic = true, // Active LOW
                                .initial_on     = false};

    PowerControl pc(cfg);
    TEST_ASSERT_EQUAL(ESP_OK, pc.init());

    // Turn ON (physical level should be LOW)
    ESP_LOGI(TAG, "Turning ON with inverted logic (LED should light up)");
    TEST_ASSERT_EQUAL(ESP_OK, pc.turnOn());
    TEST_ASSERT_TRUE(pc.isOn());
    visual_delay(1000);

    // Turn OFF (physical level should be HIGH)
    ESP_LOGI(TAG, "Turning OFF with inverted logic (LED should turn off)");
    TEST_ASSERT_EQUAL(ESP_OK, pc.turnOff());
    TEST_ASSERT_FALSE(pc.isOn());
    visual_delay(1000);

    // Cleanup
    TEST_ASSERT_EQUAL(ESP_OK, pc.deinit());
}

TEST_CASE("PowerControl: Blink pattern", "[power_control][control][visual]")
{
    ESP_LOGI(TAG, "Testing blink pattern (visual test)");

    PowerControl::Config cfg = {
        .gpio = TEST_GPIO_LED, .inverted_logic = false, .initial_on = false};

    PowerControl pc(cfg);
    TEST_ASSERT_EQUAL(ESP_OK, pc.init());

    // Blink 5 times
    for (int i = 0; i < 5; i++) {
        ESP_LOGI(TAG, "Blink %d/5", i + 1);
        TEST_ASSERT_EQUAL(ESP_OK, pc.turnOn());
        visual_delay(200);
        TEST_ASSERT_EQUAL(ESP_OK, pc.turnOff());
        visual_delay(200);
    }

    // Cleanup
    TEST_ASSERT_EQUAL(ESP_OK, pc.deinit());
}

// ============================================================================
// 3. STATE TESTS
// ============================================================================

TEST_CASE("PowerControl: Operations without initialization",
          "[power_control][state][negative]")
{
    ESP_LOGI(TAG, "Testing operations without initialization (should fail)");

    PowerControl::Config cfg = {
        .gpio = TEST_GPIO_LED, .inverted_logic = false, .initial_on = false};

    PowerControl pc(cfg);

    // All operations should fail with invalid state
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, pc.turnOn());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, pc.turnOff());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, pc.toggle());

    // State should be false
    TEST_ASSERT_FALSE(pc.isInitialized());
    TEST_ASSERT_FALSE(pc.isOn());
}

TEST_CASE("PowerControl: Deinitialization", "[power_control][state]")
{
    ESP_LOGI(TAG, "Testing deinitialization");

    PowerControl::Config cfg = {
        .gpio = TEST_GPIO_LED, .inverted_logic = false, .initial_on = false};

    PowerControl pc(cfg);

    // Initialize and turn on
    TEST_ASSERT_EQUAL(ESP_OK, pc.init());
    TEST_ASSERT_EQUAL(ESP_OK, pc.turnOn());
    TEST_ASSERT_TRUE(pc.isOn());
    TEST_ASSERT_TRUE(pc.isInitialized());

    ESP_LOGI(TAG, "LED should be ON");
    visual_delay(1000);

    // Deinitialize
    ESP_LOGI(TAG, "Deinitializing...");
    TEST_ASSERT_EQUAL(ESP_OK, pc.deinit());

    // State should be reset
    TEST_ASSERT_FALSE(pc.isInitialized());
    TEST_ASSERT_FALSE(pc.isOn());

    ESP_LOGI(TAG, "LED should be OFF after deinit");
    visual_delay(1000);

    // Operations should fail after deinit
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, pc.turnOn());
}

TEST_CASE("PowerControl: Multiple deinit calls", "[power_control][state]")
{
    ESP_LOGI(TAG, "Testing multiple deinit calls (should be safe)");

    PowerControl::Config cfg = {
        .gpio = TEST_GPIO_LED, .inverted_logic = false, .initial_on = false};

    PowerControl pc(cfg);

    TEST_ASSERT_EQUAL(ESP_OK, pc.init());

    // First deinit
    TEST_ASSERT_EQUAL(ESP_OK, pc.deinit());
    TEST_ASSERT_FALSE(pc.isInitialized());

    // Second deinit should also succeed (idempotent)
    TEST_ASSERT_EQUAL(ESP_OK, pc.deinit());
    TEST_ASSERT_FALSE(pc.isInitialized());
}

// ============================================================================
// 4. MULTIPLE INSTANCES TESTS
// ============================================================================

TEST_CASE("PowerControl: Multiple instances with different GPIOs",
          "[power_control][multi]")
{
    ESP_LOGI(TAG, "Testing multiple instances simultaneously");

    PowerControl::Config cfg1 = {
        .gpio = TEST_GPIO_VALID_1, .inverted_logic = false, .initial_on = false};

    PowerControl::Config cfg2 = {
        .gpio = TEST_GPIO_VALID_2, .inverted_logic = false, .initial_on = false};

    PowerControl pc1(cfg1);
    PowerControl pc2(cfg2);

    // Initialize both
    TEST_ASSERT_EQUAL(ESP_OK, pc1.init());
    TEST_ASSERT_EQUAL(ESP_OK, pc2.init());

    // Both should be initialized
    TEST_ASSERT_TRUE(pc1.isInitialized());
    TEST_ASSERT_TRUE(pc2.isInitialized());

    // Pins should be different
    TEST_ASSERT_NOT_EQUAL(pc1.getPin(), pc2.getPin());

    // Control independently
    TEST_ASSERT_EQUAL(ESP_OK, pc1.turnOn());
    TEST_ASSERT_TRUE(pc1.isOn());
    TEST_ASSERT_FALSE(pc2.isOn());

    TEST_ASSERT_EQUAL(ESP_OK, pc2.turnOn());
    TEST_ASSERT_TRUE(pc1.isOn());
    TEST_ASSERT_TRUE(pc2.isOn());

    TEST_ASSERT_EQUAL(ESP_OK, pc1.turnOff());
    TEST_ASSERT_FALSE(pc1.isOn());
    TEST_ASSERT_TRUE(pc2.isOn());

    // Cleanup
    TEST_ASSERT_EQUAL(ESP_OK, pc1.deinit());
    TEST_ASSERT_EQUAL(ESP_OK, pc2.deinit());
}

TEST_CASE("PowerControl: LED instance with multiple operations",
          "[power_control][multi]")
{
    ESP_LOGI(TAG, "Testing LED with multiple instances and operations");

    PowerControl::Config cfg_led = {
        .gpio = TEST_GPIO_LED, .inverted_logic = false, .initial_on = false};

    PowerControl::Config cfg_gpio = {
        .gpio = TEST_GPIO_VALID_1, .inverted_logic = false, .initial_on = false};

    PowerControl led(cfg_led);
    PowerControl gpio(cfg_gpio);

    TEST_ASSERT_EQUAL(ESP_OK, led.init());
    TEST_ASSERT_EQUAL(ESP_OK, gpio.init());

    // Alternate blinking
    for (int i = 0; i < 3; i++) {
        ESP_LOGI(TAG, "LED ON, GPIO OFF");
        led.turnOn();
        gpio.turnOff();
        visual_delay(300);

        ESP_LOGI(TAG, "LED OFF, GPIO ON");
        led.turnOff();
        gpio.turnOn();
        visual_delay(300);
    }

    // Cleanup
    TEST_ASSERT_EQUAL(ESP_OK, led.deinit());
    TEST_ASSERT_EQUAL(ESP_OK, gpio.deinit());
}

// ============================================================================
// MAIN - Unity Test Runner
// ============================================================================

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "  PowerControl Component Unit Tests");
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Running tests on hardware (no mocks)");
    ESP_LOGI(TAG, "Visual tests will use GPIO 2 (built-in LED)");
    ESP_LOGI(TAG, "");

    // Run all tests
    UNITY_BEGIN();
    unity_run_all_tests();
    UNITY_END();

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "  All tests completed!");
    ESP_LOGI(TAG, "===========================================");
}
