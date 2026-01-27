#include "esp_log.h"
#include "float_switch.hpp"
#include "unity.h"
#include "freertos/FreeRTOS.h"

#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/task.h"

static const char *TAG = "FloatSwitchTest";

#define INPUT_PIN GPIO_NUM_4
#define CTRL_PIN GPIO_NUM_5 // Connect with jumper to INPUT_PIN

/**
 * @brief Tests that the FloatSwitch can be initialized and deinitialized
 * correctly.
 */
TEST_CASE("FloatSwitch: Lifecycle (Init/Deinit)", "[float_switch][memory]")
{
    FloatSwitch::Config cfg = {};
    cfg.gpio                = INPUT_PIN;
    cfg.normally_open       = true;
    cfg.active_level        = FloatSwitch::ActiveLevel::LOW;

    FloatSwitch fs(cfg);

    // 1. Test correct initialization
    TEST_ASSERT_EQUAL(ESP_OK, fs.init());

    // 2. Test that deinit clears the state
    TEST_ASSERT_EQUAL(ESP_OK, fs.deinit());

    // 3. Test post-deinit behavior (should not abort!)
    // Should log error and return raw GPIO level
    bool level = fs.isContactClosed();
    ESP_LOGI(TAG, "Post-deinit level (fallback): %d", level);
}

/**
 * @brief Tests that an invalid GPIO is rejected during initialization.
 */
TEST_CASE("FloatSwitch: Reject Invalid GPIO", "[float_switch][validation]")
{
    // Test if the integrated GpioValidator in init() is working
    FloatSwitch::Config cfg = {};
    cfg.gpio                = GPIO_NUM_MAX; // Clearly invalid

    FloatSwitch fs(cfg);

    // Init should fail because GpioValidator should return an error
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, fs.init());
}

/**
 * @brief Tests that a warning is logged for input-only pins on ESP32 classic.
 */
TEST_CASE("FloatSwitch: ESP32 Input-Only Warning", "[float_switch][validation]")
{
#if CONFIG_IDF_TARGET_ESP32
    ESP_LOGI(TAG, "Testing Input-Only GPIO 34 (Should trigger a WARNING log)");

    FloatSwitch::Config cfg = {};
    cfg.gpio          = GPIO_NUM_34; // Pins 34-39 on classic ESP32 have no pull-up
    cfg.normally_open = true;
    cfg.active_level  = FloatSwitch::ActiveLevel::LOW;

    FloatSwitch fs(cfg);

    // Init should return ESP_OK (since the pin is valid for reading),
    // but you should see the ESP_LOGW in the serial monitor.
    TEST_ASSERT_EQUAL(ESP_OK, fs.init());

    fs.deinit();
#else
    ESP_LOGI(TAG, "Not an ESP32 Classic target, skipping Input-Only warning test.");
#endif
}

/**
 * @brief Tests the logical interpretation of the switch state.
 * This covers the translation from electrical signal to Tank State.
 */
TEST_CASE("FloatSwitch: Logic Matrix (Active LOW)", "[float_switch][logic]")
{
    gpio_set_direction(CTRL_PIN, GPIO_MODE_OUTPUT);

    // Scenario A: Normally Open (NO) + Active LOW (Contact pulls to GND)
    {
        FloatSwitch::Config cfg = {
            .gpio          = INPUT_PIN,
            .normally_open = true,
            .active_level  = FloatSwitch::ActiveLevel::LOW,
        };
        FloatSwitch fs(cfg);
        fs.init();

        // Simulate Tank Empty: Float down -> Contact Closed -> Signal 0 (GND)
        gpio_set_level(CTRL_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(50));
        TEST_ASSERT_FALSE_MESSAGE(
            fs.isTankFull(),
            "NO/Active_LOW: Level 0 should be EMPTY (Float is down)");

        // Simulate Tank Full: Float up -> Contact Open -> Signal 1 (Pull-up)
        gpio_set_level(CTRL_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
        TEST_ASSERT_TRUE_MESSAGE(
            fs.isTankFull(), "NO/Active_LOW: Level 1 should be FULL (Float is up)");
    }

    // Scenario B: Normally Closed (NC) + Active LOW
    {
        FloatSwitch::Config cfg = {.gpio          = INPUT_PIN,
                                   .normally_open = false, // NC
                                   .active_level  = FloatSwitch::ActiveLevel::LOW};
        FloatSwitch fs(cfg);
        fs.init();

        // Simulate Tank Empty: Float down -> Contact Open -> Signal 1 (Pull-up)
        gpio_set_level(CTRL_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
        TEST_ASSERT_FALSE_MESSAGE(
            fs.isTankFull(),
            "NC/Active_LOW: Level 1 should be EMPTY (Float is down)");

        // Simulate Tank Full: Float up -> Contact Open -> Signal 0 (GND)
        gpio_set_level(CTRL_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(50));
        TEST_ASSERT_TRUE_MESSAGE(
            fs.isTankFull(), "NC/Active_LOW: Level 0 should be FULL (Float is up)");
    }
}

/**
 * @brief Tests ActiveLevel::HIGH (Contact pulls to 3.3V)
 */
TEST_CASE("FloatSwitch: Logic Matrix (Active HIGH)", "[float_switch][logic]")
{
    gpio_set_direction(CTRL_PIN, GPIO_MODE_OUTPUT);

    // Scenario C: Normally Open (NO) + Active HIGH (Contact pulls to VCC)
    {
        FloatSwitch::Config cfg = {.gpio = INPUT_PIN,
                                   .normally_open =
                                       true, // Tank low -> Contact closed
                                   .active_level = FloatSwitch::ActiveLevel::HIGH};
        FloatSwitch fs(cfg);
        fs.init();

        // Simulate Tank Empty: Float down -> Contact Closed -> Signal 1 (VCC)
        gpio_set_level(CTRL_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
        TEST_ASSERT_FALSE_MESSAGE(fs.isTankFull(),
                                  "NO/Active_HIGH: Level 1 should be EMPTY");

        // Simulate Tank Full: Float up -> Contact Open -> Signal 0 (Pull-down)
        gpio_set_level(CTRL_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(50));
        TEST_ASSERT_TRUE_MESSAGE(fs.isTankFull(),
                                 "NO/Active_HIGH: Level 0 should be FULL");
    }

    // Scenario D: Normally Closed (NC) + Active HIGH
    {
        FloatSwitch::Config cfg = {.gpio          = INPUT_PIN,
                                   .normally_open = false, // NC
                                   .active_level  = FloatSwitch::ActiveLevel::HIGH};
        FloatSwitch fs(cfg);
        fs.init();

        // Simulate Tank Empty: Float down -> Contact Open -> Signal 0 (Pull-down)
        gpio_set_level(CTRL_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(50));
        TEST_ASSERT_FALSE_MESSAGE(fs.isTankFull(),
                                  "NC/Active_HIGH: Level 0 should be EMPTY");

        // Simulate Tank Full: Float up -> Contact Closed -> Signal 1 (VCC)
        gpio_set_level(CTRL_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
        TEST_ASSERT_TRUE_MESSAGE(fs.isTankFull(),
                                 "NC/Active_HIGH: Level 1 should be FULL");
    }
}

/**
 * @brief Tests the anti-loop wakeup logic.
 */
TEST_CASE("FloatSwitch: Wakeup Anti-Loop", "[float_switch][wakeup]")
{
    gpio_set_direction(CTRL_PIN, GPIO_MODE_OUTPUT);

    // Config: Wake up when tank is EMPTY
    FloatSwitch::Config cfg = {.gpio          = INPUT_PIN,
                               .normally_open = true,
                               .active_level  = FloatSwitch::ActiveLevel::LOW,
                               .wakeup_on =
                                   FloatSwitch::WakeupCondition::WHEN_TANK_IS_EMPTY};
    FloatSwitch fs(cfg);
    fs.init();

    // Scenario 1: Tank is FULL (Float up, Signal 1)
    // Condition: Waiting for EMPTY. Current: FULL.
    // Action: ARM WAKEUP (Expect TRUE)
    gpio_set_level(CTRL_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_TRUE_MESSAGE(fs.shouldEnableWakeup(),
                             "Should arm: currently FULL, waiting for EMPTY");

    // Scenario 2: Tank is EMPTY (Float down, Signal 0)
    // Condition: Waiting for EMPTY. Current: EMPTY.
    // Action: DO NOT ARM (Expect FALSE) to avoid immediate wake loop.
    gpio_set_level(CTRL_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_FALSE_MESSAGE(fs.shouldEnableWakeup(),
                              "Should NOT arm: already EMPTY");
}

/**
 * @brief Tests the anti-loop wakeup logic for WHEN_TANK_IS_FULL condition.
 */
TEST_CASE("FloatSwitch: Wakeup Anti-Loop (Full)", "[float_switch][wakeup]")
{
    // Reuse global defines INPUT_PIN and CTRL_PIN
    gpio_set_direction(CTRL_PIN, GPIO_MODE_OUTPUT);

    FloatSwitch::Config cfg = {.gpio          = INPUT_PIN,
                               .normally_open = true,
                               .active_level  = FloatSwitch::ActiveLevel::LOW,
                               .wakeup_on =
                                   FloatSwitch::WakeupCondition::WHEN_TANK_IS_FULL};
    FloatSwitch fs(cfg);
    fs.init();

    // SCENARIO 1: Tank is currently EMPTY (Float down, Signal 0 for NO/Active_LOW)
    // Condition: Waiting for FULL. Current: EMPTY.
    // Action: SHOULD arm wakeup (Expect TRUE).
    gpio_set_level(CTRL_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_TRUE_MESSAGE(fs.shouldEnableWakeup(),
                             "Should arm: currently EMPTY, waiting for FULL");

    // SCENARIO 2: Tank is currently FULL (Float up, Signal 1)
    // Condition: Waiting for FULL. Current: FULL.
    // Action: SHOULD NOT arm (Expect FALSE) to avoid waking up in a loop.
    gpio_set_level(CTRL_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_FALSE_MESSAGE(fs.shouldEnableWakeup(),
                              "Should NOT arm: already FULL");
}

/**
 * @brief Ensures shouldEnableWakeup never returns true if wakeup is disabled.
 */
TEST_CASE("FloatSwitch: Wakeup Disabled", "[float_switch][wakeup]")
{
    FloatSwitch::Config cfg = {
        .gpio          = INPUT_PIN,
        .normally_open = true,
        .active_level  = FloatSwitch::ActiveLevel::LOW,
        .wakeup_on     = FloatSwitch::WakeupCondition::NEVER // Assuming 0 is NEVER
    };
    FloatSwitch fs(cfg);
    fs.init();

    // Tank Empty
    gpio_set_level(CTRL_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_FALSE(fs.shouldEnableWakeup());

    // Tank Full
    gpio_set_level(CTRL_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_FALSE(fs.shouldEnableWakeup());
}

/**
 * @brief Stress test the lifecycle to check for memory leaks.
 */
TEST_CASE("FloatSwitch: Stress Lifecycle", "[float_switch][memory]")
{
    FloatSwitch::Config cfg = {.gpio          = INPUT_PIN,
                               .normally_open = true,
                               .active_level  = FloatSwitch::ActiveLevel::LOW};

    size_t initial_free = heap_caps_get_free_size(MALLOC_CAP_8BIT);

    for (int i = 0; i < 100; i++) {
        FloatSwitch fs(cfg);
        fs.init();
        fs.isTankFull();
        // Destructor cleans up at the end of each iteration
    }

    size_t final_free = heap_caps_get_free_size(MALLOC_CAP_8BIT);

    // The delta should be negligible or zero
    TEST_ASSERT_UINT32_WITHIN(10, initial_free, final_free);
}

TEST_CASE("FloatSwitch: Debounce with Real-time Bouncing",
          "[float_switch][debounce][rtos]")
{
    gpio_set_direction(CTRL_PIN, GPIO_MODE_OUTPUT);

    FloatSwitch::Config cfg = {.gpio          = INPUT_PIN,
                               .normally_open = true,
                               .active_level  = FloatSwitch::ActiveLevel::LOW,
                               .wakeup_on     = FloatSwitch::WakeupCondition::NEVER};

    FloatSwitch fs(cfg);
    TEST_ASSERT_EQUAL(ESP_OK, fs.init());

    gpio_set_direction(CTRL_PIN, GPIO_MODE_OUTPUT);

    // 1. Garantir múltiplas leituras estáveis em LOW
    gpio_set_level(CTRL_PIN, 0);
    for (int i = 0; i < 3; i++) {
        vTaskDelay(pdMS_TO_TICKS(30));
        TEST_ASSERT_TRUE(fs.isContactClosed());
    }

    ESP_LOGI(TAG, "Starting timed transition test...");

    // 2. Medir TEMPO EXATO da transição
    int64_t transition_start, detection_time;

    // Primeira leitura inicia sampling
    bool first_read  = fs.isContactClosed();
    transition_start = esp_timer_get_time();

    // Imediatamente após iniciar sampling, muda para HIGH
    gpio_set_level(CTRL_PIN, 1);
    ESP_LOGI(TAG, "Transition to HIGH at %lld us", transition_start);

    // Loop de polling até detectar mudança
    bool changed = false;
    for (int i = 0; i < 10; i++) {
        vTaskDelay(pdMS_TO_TICKS(5)); // Poll a cada 5ms

        bool current = fs.isContactClosed();
        if (!current) { // Detectou HIGH (contato aberto)
            detection_time = esp_timer_get_time();
            changed        = true;
            ESP_LOGI(TAG, "Detected change at %lld us (iter %d)", detection_time, i);
            break;
        }
    }

    TEST_ASSERT_TRUE_MESSAGE(changed, "Should detect transition");

    int64_t delay_us = detection_time - transition_start;
    ESP_LOGI(TAG, "Detection delay: %lld us (%.1f ms)", delay_us, delay_us / 1000.0);

    // O delay deve ser ~20-25ms (tempo de debouncing)
    TEST_ASSERT_GREATER_THAN(15000, delay_us); // >15ms
    TEST_ASSERT_LESS_THAN(40000, delay_us);    // <40ms

    fs.deinit();
}
