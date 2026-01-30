// components/wifi_manager/test/main/test_wifi_manager_internal.cpp
#include "esp_timer.h"
#include "esp_wifi.h"
#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>

#include "test_memory_helper.h"
#include "test_wifi_manager_accessor.hpp"

// ========================================================================
// INTERNAL METHOD TESTS
// These tests focus on monitoring internal task behavior and queue health,
// which is not possible via the public API.
// ========================================================================

#ifdef UNIT_TEST

/**
 * @brief Test: Queue capacity and behavior
 *
 * Verifies that the internal command queue correctly handles bursts of commands
 * and that the task consumes them efficiently.
 */
TEST_CASE("test_internal_queue_behavior", "[wifi][internal][stress]")
{
    set_memory_leak_threshold(-2000);
    printf("\n=== Test: Queue Behaviors ===\n");

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();

    WiFiManagerTestAccessor accessor(wm);

    // Get initial queue state
    uint32_t initial_pending = accessor.test_getQueuePendingCount();
    printf("Initial pending commands: %lu\n", initial_pending);
    TEST_ASSERT_EQUAL(0, initial_pending);

    // Send multiple commands to fill the queue
    const int COMMANDS_TO_SEND = 15; // More than queue capacity (10)
    printf("Sending %d commands to test queue capacity...\n", COMMANDS_TO_SEND);

    int successful_sends = 0;
    for (int i = 0; i < COMMANDS_TO_SEND; i++) {
        if (accessor.test_sendStartCommand(true) == ESP_OK) {
            successful_sends++;
        }

        // Monitor queue size in real-time
        if (i % 5 == 0) {
            printf("  Command %2d: Queue: %lu pending\n", i + 1, accessor.test_getQueuePendingCount());
        }
    }

    printf("Successful sends: %d/%d\n", successful_sends, COMMANDS_TO_SEND);

    // Because the wifiTask has higher priority, it should consume commands
    // almost as fast as we send them.
    TEST_ASSERT_EQUAL(COMMANDS_TO_SEND, successful_sends);

    // Verify queue is not stuck
    TEST_ASSERT_FALSE(accessor.test_isQueueFull());

    // ====== CLEANUP ======
    int attempts = 0;
    while (accessor.test_getQueuePendingCount() > 0 && attempts < 50) {
        vTaskDelay(pdMS_TO_TICKS(10));
        attempts++;
    }

    printf("Final pending commands: %lu\n", accessor.test_getQueuePendingCount());
    wm.deinit();
}

/**
 * @brief Test: Stress test with internal monitoring
 *
 * Heavy load test while monitoring queue depth to ensure the task
 * doesn't lag behind the producer.
 */
TEST_CASE("test_internal_stress_with_monitoring", "[wifi][internal][stress]")
{
    set_memory_leak_threshold(-2000);
    printf("\n=== Test: Stress Test with Internal Monitoring ===\n");

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    wm.start();

    WiFiManagerTestAccessor accessor(wm);

    const int NUM_COMMANDS = 50;
    int successes          = 0;

    printf("Sending %d rapid commands...\n", NUM_COMMANDS);
    int64_t start_time = esp_timer_get_time();

    for (int i = 0; i < NUM_COMMANDS; i++) {
        esp_err_t ret;
        if (i % 2 == 0) {
            ret = accessor.test_sendStartCommand(true);
        } else {
            ret = accessor.test_sendConnectCommand("StressSSID", "password", true);
        }

        if (ret == ESP_OK) successes++;

        if (i % 10 == 0) {
            printf("  Progress %d/%d, Queue: %lu pending\n", i, NUM_COMMANDS, accessor.test_getQueuePendingCount());
        }
    }

    int64_t end_time = esp_timer_get_time();
    float elapsed_ms = (end_time - start_time) / 1000.0f;

    printf("\nResults: %.1f ms (%.1f cmd/sec), Successes: %d\n",
           elapsed_ms, NUM_COMMANDS / (elapsed_ms / 1000.0f), successes);

    // Let task catch up
    int attempts = 0;
    while (accessor.test_getQueuePendingCount() > 0 && attempts < 100) {
        vTaskDelay(pdMS_TO_TICKS(10));
        attempts++;
    }

    TEST_ASSERT_EQUAL(0, accessor.test_getQueuePendingCount());
    TEST_ASSERT(accessor.test_getInternalState() >= WiFiManager::State::STARTED);

    wm.deinit();
}

/**
 * @brief Test: Reconnection Logic and Backoff
 *
 * Verifies that the manager correctly identifies terminal (AUTH_FAIL)
 * vs temporary failures and enters the correct states.
 */
TEST_CASE("test_internal_reconnection_logic", "[wifi][internal][reconnect]")
{
    set_memory_leak_threshold(-2000);
    printf("\n=== Test: Reconnection Logic ===\n");

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    wm.start();

    WiFiManagerTestAccessor accessor(wm);

    // 1. Simulate a connection attempt
    printf("Simulating CONNECT command...\n");
    accessor.test_sendConnectCommand("RetrySSID", "password", true);
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(WiFiManager::State::CONNECTING, wm.getState());

    // 2. Simulate AUTH_FAIL (Terminal)
    printf("Simulating AUTH_FAIL...\n");
    accessor.test_simulateDisconnect(WIFI_REASON_AUTH_FAIL);
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL(WiFiManager::State::ERROR_CREDENTIALS, wm.getState());

    // 3. Reset and try a temporary failure
    printf("Resetting with new CONNECT...\n");
    accessor.test_sendConnectCommand("RetrySSID", "password", true);
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(WiFiManager::State::CONNECTING, wm.getState());

    // 4. Simulate NO_AP_FOUND (Retryable)
    printf("Simulating NO_AP_FOUND...\n");
    accessor.test_simulateDisconnect(WIFI_REASON_NO_AP_FOUND);
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL(WiFiManager::State::WAITING_RECONNECT, wm.getState());

    // 5. Verify manual interrupt
    printf("Interrupting backoff with manual DISCONNECT...\n");
    wm.disconnect_async();
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL(WiFiManager::State::DISCONNECTED, wm.getState());

    wm.deinit();
}

#endif // UNIT_TEST
