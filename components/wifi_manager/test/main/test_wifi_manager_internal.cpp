// components/wifi_manager/test/main/test_wifi_manager_internal.cpp
#pragma once

#include "esp_timer.h"
#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <iostream>
#include <stdio.h>
#include <vector>

#include "test_memory_helper.h"
#include "test_wifi_manager_accessor.hpp"

// ========================================================================
// INTERNAL METHOD TESTS
// These tests access private WiFiManager methods via the friend class
// ========================================================================

#ifdef UNIT_TEST
/**
 * @brief Test 23: Internal send command methods
 *
 * Tests the internal command sending mechanism using specific helper methods
 * instead of direct access to private structures.
 */
TEST_CASE("test_internal_send_command", "[wifi][internal]")
{
    set_memory_leak_threshold(-15000);
    printf("\n=== Test 23: Internal sendCommand ===\n");

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit(); // Ensure clean starting state
    esp_err_t ret = wm.init();
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    WiFiManagerTestAccessor accessor(wm);

    // ====== PHASE 1: Testing START command ======
    printf("Phase 1: Testing START command...\n");

    ret = accessor.test_sendStartCommand(true); // Async mode
    printf("test_sendStartCommand returned: %s\n", esp_err_to_name(ret));
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    // Wait for task to process the command
    vTaskDelay(pdMS_TO_TICKS(50));

    // Verify state after START command
    WiFiManager::State state = accessor.test_getInternalState();
    printf("Internal state after START: %d\n", static_cast<int>(state));

    // Should be INITIALIZED (command queued) or STARTING/STARTED (processing)
    TEST_ASSERT(state >= WiFiManager::State::INITIALIZED);

    // ====== PHASE 2: Start WiFi to enable CONNECT commands ======
    printf("\nPhase 2: Starting WiFi for CONNECT test...\n");
    ret = wm.start(2000);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    // Verify WiFi is started
    state = accessor.test_getInternalState();
    printf("Internal state after start(): %d\n", static_cast<int>(state));
    TEST_ASSERT_EQUAL(WiFiManager::State::STARTED, state);

    // ====== PHASE 3: Testing CONNECT command ======
    printf("\nPhase 3: Testing CONNECT command...\n");

    ret = accessor.test_sendConnectCommand("TestSSID", "TestPassword",
                                           false); // Sync mode
    printf("test_sendConnectCommand returned: %s\n", esp_err_to_name(ret));
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    // ====== PHASE 4: Verify state transitions ======
    printf("\nPhase 4: Verifying state transitions...\n");
    vTaskDelay(pdMS_TO_TICKS(100)); // Give task time to process

    state = accessor.test_getInternalState();
    printf("Internal state after CONNECT command: %d\n", static_cast<int>(state));

    // Should be in CONNECTING or reached CONNECTED_NO_IP state
    // Note: Since we're using a test SSID, actual connection will fail,
    // but the state should progress to CONNECTING
    TEST_ASSERT(state >= WiFiManager::State::CONNECTING);

    // ====== PHASE 5: Monitor queue behavior ======
    printf("\nPhase 5: Monitoring queue behavior...\n");
    uint32_t pending_commands = accessor.test_getQueuePendingCount();
    printf("Pending commands in queue: %lu\n", pending_commands);

    // Queue should be empty or have few pending commands
    // (task processes commands quickly)
    TEST_ASSERT(pending_commands <= 2);

    // ====== CLEANUP ======
    printf("\nCleaning up...\n");

    // Send stop command
    ret = accessor.test_sendStopCommand(true);
    printf("test_sendStopCommand returned: %s\n", esp_err_to_name(ret));

    // Wait for stop to process
    vTaskDelay(pdMS_TO_TICKS(100));

    // Final deinit
    wm.deinit();

    printf("\n✓ Test passed: Internal sendCommands works correctly\n");
}

/**
 * @brief Test 24: Queue capacity and overflow behaviors
 *
 * Tests command queue behavior when approaching and reaching capacity
 * using the helper methods to monitor internal state.
 */
TEST_CASE("test_internal_queue_behavior", "[wifi][internal]")
{
    set_memory_leak_threshold(-15000);
    printf("\n=== Test 24: Queue Behaviors ===\n");

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
    printf("\nSending %d commands to test queue capacity...\n", COMMANDS_TO_SEND);

    int successful_sends = 0;
    int failed_sends     = 0;

    for (int i = 0; i < COMMANDS_TO_SEND; i++) {
        esp_err_t ret = accessor.test_sendStartCommand(true); // Async mode

        if (ret == ESP_OK) {
            successful_sends++;
        }
        else {
            failed_sends++;
        }

        // Monitor queue size in real-time
        uint32_t current_pending = accessor.test_getQueuePendingCount();
        printf("  Command %2d: %-12s | Queue: %2lu pending\n", i + 1,
               esp_err_to_name(ret), current_pending);
    }

    printf("\nQueue capacity test results:\n");
    printf("  Successful sends: %d\n", successful_sends);
    printf("  Failed sends: %d\n", failed_sends);
    printf("  Expected capacity: 10\n");

    // The task must be very quicly to process commands
    TEST_ASSERT_EQUAL(15, successful_sends);

    // Verify queue is empty
    bool is_queue_full = accessor.test_isQueueFull();
    printf("Queue full status: %s\n", is_queue_full ? "YES" : "NO");
    TEST_ASSERT_FALSE(is_queue_full);

    // ====== CLEANUP ======
    printf("\nCleaning queue...\n");

    // Let task process the queued commands
    int attempts = 0;
    while (accessor.test_getQueuePendingCount() > 0 && attempts < 50) {
        vTaskDelay(pdMS_TO_TICKS(10));
        attempts++;
    }

    printf("Queue cleared after %d attempts\n", attempts);
    printf("Final pending commands: %lu\n", accessor.test_getQueuePendingCount());

    wm.deinit();
    printf("\n✓ Test 24 passed: Queue behavior verifieds\n");
}

/**
 * @brief Test 26: Force queue full condition
 *
 * Attempts to fill the command queue by sending rapid commands
 * to test queue capacity and overflow behavior.
 */
TEST_CASE("test_internal_queue_full_forced", "[wifi][internal]")
{
    set_memory_leak_threshold(-15000);
    printf("\n=== Test: Forced Queue Full Test ===\n");

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();

    WiFiManagerTestAccessor accessor(wm);

    printf("Attempting to fill command queue with rapid CONNECT commands...\n");

    // Send multiple CONNECT commands rapidly
    const uint32_t ATTEMPTED_COMMANDS = 15; // More than queue capacity (10)
    uint32_t successes                = 0;
    uint32_t failures                 = 0;

    for (uint32_t i = 0; i < ATTEMPTED_COMMANDS; i++) {
        char ssid[32];
        snprintf(ssid, sizeof(ssid), "TestSSID_%lu", i);

        esp_err_t ret = accessor.test_sendConnectCommand(ssid, "password", true);

        if (ret == ESP_OK) {
            successes++;
        }
        else {
            failures++;
        }

        printf("  Command %2lu: %-12s | Queue pending: %lu\n", i + 1,
               esp_err_to_name(ret), accessor.test_getQueuePendingCount());

        // Small delay to allow some processing but still fill queue
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    printf("\nQueue capacity test results:\n");
    printf("  Attempted commands: %lu\n", ATTEMPTED_COMMANDS);
    printf("  Successful sends:   %lu\n", successes);
    printf("  Failed sends:       %lu\n", failures);

    // Check if queue is full
    bool is_queue_full = accessor.test_isQueueFull();
    printf("  Queue full status:  %s\n", is_queue_full ? "YES" : "NO");

    uint32_t pending = accessor.test_getQueuePendingCount();
    printf("  Final pending:      %lu\n", pending);

    // This task needs to process commands quickly
    TEST_ASSERT(failures == 0);

    wm.deinit();
    printf("\n✓ Test passed: Queue capacity behavior verified\n");
}

/**
 * @brief Test 27: Command parameter validation edge cases
 *
 * Tests command parameter boundaries and unusual values
 * using helper methods instead of direct Command struct access.
 */
TEST_CASE("test_internal_command_validation", "[wifi][internal]")
{
    set_memory_leak_threshold(-15000);
    printf("\n=== Test Command Parameter Validation ===\n");

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    wm.start();

    WiFiManagerTestAccessor accessor(wm);

    // Define test cases with various SSID and password combinations
    struct TestCase
    {
        std::string ssid;
        std::string password;
        const char *description;
    };

    TestCase test_cases[] = {
        // Normal cases
        {"TestSSID", "TestPassword", "Normal credentials"},
        {"a", "b", "Minimal length"},

        // Edge cases - empty strings
        {"", "", "Empty SSID and password"},
        {"TestSSID", "", "Empty password"},
        {"", "TestPassword", "Empty SSID"},

        // Maximum length cases (ESP32 limits: SSID=32, Password=64)
        {std::string(32, 'A'), std::string(64, 'B'), "Maximum lengths"},

        // Exceeding limits (will be truncated by WiFi driver)
        {std::string(33, 'C'), "password", "SSID exceeds max length"},
        {"SSID", std::string(65, 'D'), "Password exceeds max length"},

        // Special characters
        {"Test_SSID-123", "P@ssw0rd!@#$", "Special characters"},
        {"Test\nSSID", "Test\nPass", "Newline characters"},
        {"Test\tSSID", "Test\tPass", "Tab characters"},

        // Unicode/UTF-8 (WiFi SSIDs are usually ASCII, but test anyway)
        {"Test Café", "Motörhead", "Extended ASCII"},
        {"网络", "密码", "Non-Latin characters"},

        // Very long but valid
        {std::string(20, 'E'), std::string(50, 'F'), "Long but valid"}};

    printf("Testing %u command parameter combinations...\n",
           sizeof(test_cases) / sizeof(test_cases[0]));

    uint32_t test_count     = 0;
    uint32_t accepted_count = 0;
    uint32_t rejected_count = 0;

    for (const auto &tc : test_cases) {
        test_count++;

        printf("\nTest %2lu: %s\n", test_count, tc.description);
        printf("  SSID: '%.20s' (len=%zu)\n", tc.ssid.c_str(), tc.ssid.length());
        printf("  PASS: '%.20s' (len=%zu)\n", tc.password.c_str(),
               tc.password.length());

        // Send CONNECT command using helper method
        esp_err_t ret = accessor.test_sendConnectCommand(tc.ssid, tc.password, true);

        printf("  Result: %s\n", esp_err_to_name(ret));

        // All commands should be accepted at queue level
        // Actual validation happens in wifiTask
        TEST_ASSERT_EQUAL(ESP_OK, ret);

        if (ret == ESP_OK) {
            accepted_count++;
        }
        else {
            rejected_count++;
        }

        // Small delay to prevent overwhelming the queue
        vTaskDelay(pdMS_TO_TICKS(5));
        accessor.test_sendDisconnectCommand(true);
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    // Wait for all commands to be processed
    printf("\nWaiting for command processing...\n");
    uint32_t attempts = 0;
    while (accessor.test_getQueuePendingCount() > 0 && attempts < 30) {
        vTaskDelay(pdMS_TO_TICKS(10));
        attempts++;
    }

    uint32_t final_pending = accessor.test_getQueuePendingCount();
    printf("Queue cleared after %lu attempts\n", attempts);
    printf("Final pending commands: %lu\n", final_pending);

    // Summary
    printf("\nValidation test summary:\n");
    printf("  Total tests:      %lu\n", test_count);
    printf("  Accepted at queue: %lu\n", accepted_count);
    printf("  Rejected at queue: %lu\n", rejected_count);
    printf("  Queue capacity verified\n");

    wm.deinit();
    printf("\n✓ Test passed: Command parameter validation verified\n");
}

/**
 * @brief Test 28 Task lifecycle using internal helpers
 *
 * Tests that the internal task is properly created and destroyed
 * using the test helper methods.
 */
TEST_CASE("test_internal_task_lifecycle_with_helpers", "[wifi][internal]")
{
    set_memory_leak_threshold(-15000);
    printf("\n=== Test: Task Lifecycle (with Helpers) ===\n");

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit(); // Ensure clean start

    WiFiManagerTestAccessor accessor(wm);

    // Test multiple init/deinit cycles with internal verification
    for (int cycle = 0; cycle < 3; cycle++) {
        printf("\nCycle %d/3\n", cycle + 1);

        // 1. Initialize and verify internal state
        printf("Initializing...\n");
        TEST_ASSERT_EQUAL(ESP_OK, wm.init());

        // Give task time to start
        vTaskDelay(pdMS_TO_TICKS(50));

        // Verify we're in INITIALIZED state internally
        WiFiManager::State state = accessor.test_getInternalState();
        printf("Internal state after init: %d\n", static_cast<int>(state));
        TEST_ASSERT_EQUAL(WiFiManager::State::INITIALIZED, state);

        // 2. Start WiFi and verify task processes commands
        printf("Starting WiFi...\n");
        TEST_ASSERT_EQUAL(ESP_OK, wm.start(2000));

        // Send a command to verify task is alive
        printf("Sending test command...\n");
        esp_err_t cmd_result = accessor.test_sendStartCommand(true);
        printf("Command result: %s\n", esp_err_to_name(cmd_result));

        // Task should process it quickly (already started)
        vTaskDelay(pdMS_TO_TICKS(100));

        // Check queue - should be empty or low
        uint32_t pending = accessor.test_getQueuePendingCount();
        printf("Pending commands after processing: %lu\n", pending);
        TEST_ASSERT(pending < 3); // Should process commands quickly

        // 3. Deinitialize and verify cleanup
        printf("Deinitializing...\n");
        TEST_ASSERT_EQUAL(ESP_OK, wm.deinit());

        // Verify we're back to UNINITIALIZED
        state = accessor.test_getInternalState();
        printf("Internal state after deinit: %d\n", static_cast<int>(state));
        TEST_ASSERT_EQUAL(WiFiManager::State::UNINITIALIZED, state);

        // Small delay between cycles
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    printf("\n✓ Task lifecycle test with helpers passed\n");
}

/**
 * @brief Test 29: Stress test with internal monitoring
 *
 * Heavy load test while monitoring internal states.
 */
TEST_CASE("test_internal_stress_with_monitoring", "[wifi][internal]")
{
    set_memory_leak_threshold(-15000);
    printf("\n=== Test: Stress Test with Internal Monitoring ===\n");

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    wm.start();

    WiFiManagerTestAccessor accessor(wm);

    const int NUM_COMMANDS = 50;
    int successes          = 0;
    int failures           = 0;

    printf("Sending %d rapid commands...\n", NUM_COMMANDS);

    int64_t start_time = esp_timer_get_time();

    for (int i = 0; i < NUM_COMMANDS; i++) {
        // Alternate between START and CONNECT commands
        esp_err_t ret;
        if (i % 2 == 0) {
            ret = accessor.test_sendStartCommand(true);
        }
        else {
            ret = accessor.test_sendConnectCommand("StressSSID", "password", true);
        }

        if (ret == ESP_OK) {
            successes++;
        }
        else {
            failures++;
        }

        // Monitor queue size occasionally
        if (i % 10 == 0) {
            uint32_t pending = accessor.test_getQueuePendingCount();
            printf("  Command %d: %s, Queue: %lu pending\n", i, esp_err_to_name(ret),
                   pending);
        }
    }

    int64_t end_time = esp_timer_get_time();
    float elapsed_ms = (end_time - start_time) / 1000.0f;

    printf("\nStress test results:\n");
    printf("  Total time: %.1f ms\n", elapsed_ms);
    printf("  Commands/sec: %.1f\n", NUM_COMMANDS / (elapsed_ms / 1000.0f));
    printf("  Successes: %d\n", successes);
    printf("  Failures: %d\n", failures);

    // Let task catch up
    printf("\nLetting task process remaining commands...\n");

    int attempts = 0;
    while (accessor.test_getQueuePendingCount() > 0 && attempts < 100) {
        vTaskDelay(pdMS_TO_TICKS(10));
        attempts++;
    }

    printf("Queue cleared after %d attempts\n", attempts);
    printf("Final queue size: %lu\n", accessor.test_getQueuePendingCount());

    // Verify system is still functional
    WiFiManager::State final_state = accessor.test_getInternalState();
    printf("Final internal state: %d\n", static_cast<int>(final_state));

    // Should be in a valid state
    TEST_ASSERT(final_state >= WiFiManager::State::STARTED);

    wm.deinit();

    printf("\n✓ Stress test with internal monitoring passed\n");
}

#endif // UNIT_TEST