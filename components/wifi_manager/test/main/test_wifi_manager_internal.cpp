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
// GROUP 4: INTERNAL SIMULATION
// These tests use the TestAccessor to simulate driver events and verify
// the state machine logic without requiring a real Access Point.
// ========================================================================

#ifdef UNIT_TEST

/**
 * 24. Test Queue capacity and behavior
 */
TEST_CASE("test_internal_queue_behavior", "[wifi][internal][stress]")
{
    set_memory_leak_threshold(-2000);
    printf("\n=== Test: Queue Behaviors ===\n");

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();

    WiFiManagerTestAccessor accessor(wm);

    const int COMMANDS_TO_SEND = 15;
    int successful_sends       = 0;
    for (int i = 0; i < COMMANDS_TO_SEND; i++) {
        if (accessor.test_sendStartCommand(true) == ESP_OK) {
            successful_sends++;
        }
    }

    TEST_ASSERT_EQUAL(COMMANDS_TO_SEND, successful_sends);
    vTaskDelay(pdMS_TO_TICKS(100));
    wm.deinit();
}

/**
 * 25. Test Full connection flow simulation
 */
TEST_CASE("test_internal_connection_flow", "[wifi][internal][state]")
{
    set_memory_leak_threshold(-2000);
    printf("\n=== Test: Connection Flow Simulation ===\n");

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    WiFiManagerTestAccessor accessor(wm);

    // 1. Start WiFi
    printf("Starting WiFi...\n");
    wm.start(); // Async
    vTaskDelay(pdMS_TO_TICKS(1));
    TEST_ASSERT_EQUAL(WiFiManager::State::STARTING, wm.getState());

    printf("Simulating WIFI_EVENT_STA_START...\n");
    accessor.test_simulateWifiEvent(WIFI_EVENT_STA_START);
    vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_EQUAL(WiFiManager::State::STARTED, wm.getState());

    // 2. Connect
    printf("Setting credentials...\n");
    wm.setCredentials("SimulatedSSID", "SimulatedPass");
    printf("Connecting...\n");
    wm.connect(); // Async
    vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_EQUAL(WiFiManager::State::CONNECTING, wm.getState());

    printf("Simulating WIFI_EVENT_STA_CONNECTED...\n");
    accessor.test_simulateWifiEvent(WIFI_EVENT_STA_CONNECTED);
    vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_EQUAL(WiFiManager::State::CONNECTED_NO_IP, wm.getState());

    printf("Simulating IP_EVENT_STA_GOT_IP...\n");
    accessor.test_simulateIpEvent(IP_EVENT_STA_GOT_IP);
    vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_EQUAL(WiFiManager::State::CONNECTED_GOT_IP, wm.getState());

    wm.deinit();
}

/**
 * 26. Test Auto-reconnect on loss
 */
TEST_CASE("test_internal_auto_reconnect", "[wifi][internal][reconnect]")
{
    set_memory_leak_threshold(-2000);
    printf("\n=== Test: Auto-Reconnect Simulation ===\n");

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    wm.start(5000);
    WiFiManagerTestAccessor accessor(wm);

    accessor.test_simulateWifiEvent(WIFI_EVENT_STA_START);
    wm.setCredentials("ReconnectSSID", "pass");

    // Move to connected state
    accessor.test_sendConnectCommand(false);
    accessor.test_simulateWifiEvent(WIFI_EVENT_STA_CONNECTED);
    accessor.test_simulateIpEvent(IP_EVENT_STA_GOT_IP);
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(WiFiManager::State::CONNECTED_GOT_IP, wm.getState());

    // Connection lost (Recoverable reason: Beacon Timeout)
    printf("Simulating Beacon Timeout...\n");
    accessor.test_simulateDisconnect(WIFI_REASON_BEACON_TIMEOUT);
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL(WiFiManager::State::WAITING_RECONNECT, wm.getState());

    wm.deinit();
}

/**
 * 27. Test Immediate invalidation logic
 */
TEST_CASE("test_internal_immediate_invalidation", "[wifi][internal][reconnect]")
{
    set_memory_leak_threshold(-2000);
    printf("\n=== Test: Immediate Invalidation Simulation ===\n");

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    wm.start(5000);
    WiFiManagerTestAccessor accessor(wm);

    wm.setCredentials("InvalidPassSSID", "wrong");
    TEST_ASSERT_TRUE(wm.isCredentialsValid());

    // 4-Way Handshake Timeout (Reason 15) - Expected immediate invalidation
    printf("Simulating 4-Way Handshake Timeout (Reason 15)...\n");
    accessor.test_simulateDisconnect(WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT);
    vTaskDelay(pdMS_TO_TICKS(100));

    TEST_ASSERT_EQUAL(WiFiManager::State::ERROR_CREDENTIALS, wm.getState());
    TEST_ASSERT_FALSE(wm.isCredentialsValid());

    wm.deinit();
}

/**
 * 28. Test Suspect failure 3-strike logic
 */
TEST_CASE("test_internal_3_strikes", "[wifi][internal][reconnect]")
{
    set_memory_leak_threshold(-2000);
    printf("\n=== Test: Suspect Failure 3-Strikes Simulation ===\n");

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    wm.start(5000);
    WiFiManagerTestAccessor accessor(wm);

    wm.setCredentials("SuspectSSID", "pass");

    // Strike 1
    printf("Strike 1 (Reason 205)...\n");
    accessor.test_simulateDisconnect(WIFI_REASON_CONNECTION_FAIL);
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL(WiFiManager::State::WAITING_RECONNECT, wm.getState());
    TEST_ASSERT_TRUE(wm.isCredentialsValid());

    // Strike 2
    printf("Strike 2 (Reason 205)...\n");
    accessor.test_simulateDisconnect(WIFI_REASON_CONNECTION_FAIL);
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL(WiFiManager::State::WAITING_RECONNECT, wm.getState());

    // Strike 3 -> Invalidation
    printf("Strike 3 -> Expecting Invalidation...\n");
    accessor.test_simulateDisconnect(WIFI_REASON_CONNECTION_FAIL);
    vTaskDelay(pdMS_TO_TICKS(100));

    TEST_ASSERT_EQUAL(WiFiManager::State::ERROR_CREDENTIALS, wm.getState());
    TEST_ASSERT_FALSE(wm.isCredentialsValid());

    wm.deinit();
}

/**
 * 29. Test Manual interrupt during backoff
 */
TEST_CASE("test_internal_interrupt_backoff", "[wifi][internal][reconnect]")
{
    set_memory_leak_threshold(-2000);
    printf("\n=== Test: Manual Interrupt Simulation ===\n");

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    wm.start(5000);
    WiFiManagerTestAccessor accessor(wm);

    wm.setCredentials("InterruptSSID", "pass");
    accessor.test_simulateDisconnect(WIFI_REASON_NO_AP_FOUND);
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL(WiFiManager::State::WAITING_RECONNECT, wm.getState());

    printf("Interrupting backoff with manual disconnect()...\n");
    wm.disconnect(); // Async call to avoid blocking simulation
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL(WiFiManager::State::DISCONNECTED, wm.getState());

    wm.deinit();
}

/**
 * 30. Test Mixed Async Stress
 */
TEST_CASE("test_internal_mixed_stress", "[wifi][internal][stress]")
{
    set_memory_leak_threshold(-2000);
    printf("\n=== Test: Mixed Async Stress ===\n");

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();

    printf("Spamming mixed commands...\n");
    wm.start();
    wm.connect();
    wm.disconnect();
    wm.stop();
    wm.start();
    wm.connect();

    // Give it time to process the queue
    vTaskDelay(pdMS_TO_TICKS(500));

    // Check if it reached a valid state (should be CONNECTING or similar based on last commands)
    WiFiManager::State s = wm.getState();
    printf("Final state after stress: %d\n", (int)s);
    TEST_ASSERT(s != WiFiManager::State::UNINITIALIZED);

    wm.deinit();
}

/**
 * 31. Test Unexpected Orphan Events
 */
TEST_CASE("test_internal_unexpected_events", "[wifi][internal][robustness]")
{
    set_memory_leak_threshold(-2000);
    printf("\n=== Test: Unexpected Orphan Events ===\n");

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    WiFiManagerTestAccessor accessor(wm);

    // WiFi is INITIALIZED but not STARTED
    printf("Simulating GOT_IP while STOPPED...\n");
    accessor.test_simulateIpEvent(IP_EVENT_STA_GOT_IP);
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(WiFiManager::State::INITIALIZED, wm.getState()); // Should remain INITIALIZED

    wm.start(5000);
    accessor.test_simulateWifiEvent(WIFI_EVENT_STA_START);
    TEST_ASSERT_EQUAL(WiFiManager::State::STARTED, wm.getState());

    printf("Simulating STA_CONNECTED while STARTED but not CONNECTING...\n");
    accessor.test_simulateWifiEvent(WIFI_EVENT_STA_CONNECTED);
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(WiFiManager::State::STARTED, wm.getState()); // Should remain STARTED

    wm.deinit();
}

static void concurrent_api_task(void *pvParameters)
{
    WiFiManager &wm = WiFiManager::instance();
    for (int i = 0; i < 10; i++) {
        wm.connect();
        vTaskDelay(pdMS_TO_TICKS(5));
        wm.disconnect();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    vTaskDelete(NULL);
}

/**
 * 32. Test Concurrent API Access
 */
TEST_CASE("test_internal_concurrent_api", "[wifi][internal][concurrency]")
{
    set_memory_leak_threshold(-2000);
    printf("\n=== Test: Concurrent API Access ===\n");

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    wm.start(5000);

    printf("Launching concurrent API tasks...\n");
    xTaskCreate(concurrent_api_task, "task1", 4096, NULL, 5, NULL);
    xTaskCreate(concurrent_api_task, "task2", 4096, NULL, 5, NULL);

    vTaskDelay(pdMS_TO_TICKS(500));

    // Verify system still responsive
    TEST_ASSERT_EQUAL(ESP_OK, wm.deinit());
}

// ========================================================================
// EXHAUSTIVE FSM MATRIX TESTS
// ========================================================================

/**
 * 33. Exhaustive Command Matrix - UNINITIALIZED
 */
TEST_CASE("test_fsm_matrix_uninitialized", "[wifi][internal][matrix]")
{
    printf("\n=== Test: FSM Matrix - UNINITIALIZED ===\n");
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();

    TEST_ASSERT_EQUAL(WiFiManager::State::UNINITIALIZED, wm.getState());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, wm.start());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, wm.stop());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, wm.connect());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, wm.disconnect());
}

/**
 * 34. Exhaustive Command Matrix - INITIALIZED
 */
TEST_CASE("test_fsm_matrix_initialized", "[wifi][internal][matrix]")
{
    printf("\n=== Test: FSM Matrix - INITIALIZED ===\n");
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();

    printf("State: %d\n", (int)wm.getState());
    TEST_ASSERT_EQUAL(WiFiManager::State::INITIALIZED, wm.getState());

    // START should work
    printf("Testing START in INITIALIZED...\n");
    wm.start(); // Async
    vTaskDelay(pdMS_TO_TICKS(1));
    TEST_ASSERT_EQUAL(WiFiManager::State::STARTING, wm.getState());

    // Simulate start finished
    WiFiManagerTestAccessor(wm).test_simulateWifiEvent(WIFI_EVENT_STA_START);
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(WiFiManager::State::STARTED, wm.getState());

    wm.deinit();
    wm.init();
    printf("Testing others in INITIALIZED (sync to check INVALID_STATE_BIT)...\n");
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, wm.stop(100));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, wm.connect(100));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, wm.disconnect(100));
    wm.deinit();
}

/**
 * 35. Exhaustive Command Matrix - STARTED
 */
TEST_CASE("test_fsm_matrix_started", "[wifi][internal][matrix]")
{
    printf("\n=== Test: FSM Matrix - STARTED ===\n");
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    wm.start(5000);
    WiFiManagerTestAccessor accessor(wm);
    accessor.test_simulateWifiEvent(WIFI_EVENT_STA_START);
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(WiFiManager::State::STARTED, wm.getState());

    // In STARTED:
    printf("Testing START (redundant) in STARTED...\n");
    TEST_ASSERT_EQUAL(ESP_OK, wm.start(1000)); // Redundant, returns OK

    printf("Testing CONNECT in STARTED...\n");
    wm.connect(); // Async
    vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_EQUAL(WiFiManager::State::CONNECTING, wm.getState());

    // Reset to STARTED
    accessor.test_simulateDisconnect(WIFI_REASON_ASSOC_LEAVE);
    vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_EQUAL(WiFiManager::State::DISCONNECTED, wm.getState());

    // Manual move back to STARTED usually happens via events or another start call
    wm.start();
    accessor.test_simulateWifiEvent(WIFI_EVENT_STA_START);
    vTaskDelay(pdMS_TO_TICKS(10));

    printf("Testing DISCONNECT in STARTED...\n");
    TEST_ASSERT_EQUAL(ESP_OK,
                      wm.disconnect(1000)); // Already disconnected/not connected, returns OK

    printf("Testing STOP in STARTED...\n");
    wm.stop(); // Async
    TEST_ASSERT_EQUAL(WiFiManager::State::STOPPING, wm.getState());
    accessor.test_simulateWifiEvent(WIFI_EVENT_STA_STOP);
    vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_EQUAL(WiFiManager::State::STOPPED, wm.getState());

    wm.deinit();
}

/**
 * 36. Event Strictness - Verification of new guards
 */
TEST_CASE("test_event_strictness_guards", "[wifi][internal][strict]")
{
    printf("\n=== Test: Event Strictness Guards ===\n");
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    WiFiManagerTestAccessor accessor(wm);

    // 1. STA_START while INITIALIZED (not STARTING)
    printf("Simulating STA_START while INITIALIZED...\n");
    accessor.test_simulateWifiEvent(WIFI_EVENT_STA_START);
    vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_EQUAL(WiFiManager::State::INITIALIZED, wm.getState()); // Should be ignored

    // 2. STA_STOP while STARTED (not STOPPING)
    wm.start(5000);
    accessor.test_simulateWifiEvent(WIFI_EVENT_STA_START);
    vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_EQUAL(WiFiManager::State::STARTED, wm.getState());

    printf("Simulating STA_STOP while STARTED...\n");
    accessor.test_simulateWifiEvent(WIFI_EVENT_STA_STOP);
    vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_EQUAL(WiFiManager::State::STARTED, wm.getState()); // Should be ignored

    // 3. STA_DISCONNECTED while STOPPING (should stay STOPPING)
    printf("Simulating STOP -> DISCONNECTED -> STOP...\n");
    wm.stop(); // Async
    TEST_ASSERT_EQUAL(WiFiManager::State::STOPPING, wm.getState());
    accessor.test_simulateDisconnect(WIFI_REASON_ASSOC_LEAVE);
    TEST_ASSERT_EQUAL(WiFiManager::State::STOPPING, wm.getState()); // Should remain STOPPING

    printf("Simulating STA_STOP while STOPPING...\n");
    accessor.test_simulateWifiEvent(WIFI_EVENT_STA_STOP);
    vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_EQUAL(WiFiManager::State::STOPPED, wm.getState()); // Transition allowed

    wm.deinit();
}

/**
 * 37. GOT_IP Strictness
 */
TEST_CASE("test_got_ip_strictness", "[wifi][internal][strict]")
{
    printf("\n=== Test: GOT_IP Strictness ===\n");
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    WiFiManagerTestAccessor accessor(wm);

    // GOT_IP while STARTED (but not CONNECTING)
    wm.start(5000);
    accessor.test_simulateWifiEvent(WIFI_EVENT_STA_START);
    vTaskDelay(pdMS_TO_TICKS(10));

    printf("Simulating GOT_IP while STARTED...\n");
    accessor.test_simulateIpEvent(IP_EVENT_STA_GOT_IP);
    vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_EQUAL(WiFiManager::State::STARTED, wm.getState()); // Should be ignored

    wm.deinit();
}

/**
 * 38. Exhaustive Command Matrix - CONNECTED_GOT_IP
 */
TEST_CASE("test_fsm_matrix_connected", "[wifi][internal][matrix]")
{
    printf("\n=== Test: FSM Matrix - CONNECTED_GOT_IP ===\n");
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    wm.start(5000);
    WiFiManagerTestAccessor accessor(wm);
    accessor.test_simulateWifiEvent(WIFI_EVENT_STA_START);
    wm.setCredentials("MatrixSSID", "pass");
    wm.connect();
    accessor.test_simulateWifiEvent(WIFI_EVENT_STA_CONNECTED);
    accessor.test_simulateIpEvent(IP_EVENT_STA_GOT_IP);
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL(WiFiManager::State::CONNECTED_GOT_IP, wm.getState());

    // In CONNECTED_GOT_IP:
    printf("Testing START/CONNECT (redundant)...\n");
    TEST_ASSERT_EQUAL(ESP_OK, wm.start(1000));   // Redundant OK
    TEST_ASSERT_EQUAL(ESP_OK, wm.connect(1000)); // Redundant OK

    printf("Testing DISCONNECT in CONNECTED...\n");
    wm.disconnect(); // Async
    vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_EQUAL(WiFiManager::State::DISCONNECTING, wm.getState());
    accessor.test_simulateDisconnect(WIFI_REASON_ASSOC_LEAVE);
    vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_EQUAL(WiFiManager::State::DISCONNECTED, wm.getState());

    // Go back to CONNECTED for stop test
    printf("Reconnecting for STOP test...\n");
    wm.connect();
    accessor.test_simulateWifiEvent(WIFI_EVENT_STA_CONNECTED);
    accessor.test_simulateIpEvent(IP_EVENT_STA_GOT_IP);
    vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_EQUAL(WiFiManager::State::CONNECTED_GOT_IP, wm.getState());

    printf("Testing STOP in CONNECTED...\n");
    wm.stop(); // Async
    TEST_ASSERT_EQUAL(WiFiManager::State::STOPPING, wm.getState());
    accessor.test_simulateDisconnect(WIFI_REASON_ASSOC_LEAVE);
    vTaskDelay(pdMS_TO_TICKS(10));
    accessor.test_simulateWifiEvent(WIFI_EVENT_STA_STOP);
    vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_EQUAL(WiFiManager::State::STOPPED, wm.getState());

    wm.deinit();
}

/**
 * 39. Exhaustive Command Matrix - WAITING_RECONNECT
 */
TEST_CASE("test_fsm_matrix_waiting_reconnect", "[wifi][internal][matrix]")
{
    printf("\n=== Test: FSM Matrix - WAITING_RECONNECT ===\n");
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    wm.start(5000);
    WiFiManagerTestAccessor accessor(wm);
    accessor.test_simulateWifiEvent(WIFI_EVENT_STA_START);
    wm.setCredentials("WaitSSID", "pass");

    // Trigger recoverable failure
    printf("Simulating recoverable failure...\n");
    accessor.test_simulateDisconnect(WIFI_REASON_BEACON_TIMEOUT);
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL(WiFiManager::State::WAITING_RECONNECT, wm.getState());

    // In WAITING_RECONNECT:
    printf("Testing CONNECT in WAITING_RECONNECT...\n");
    wm.connect(); // Should move to CONNECTING immediately
    vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_EQUAL(WiFiManager::State::CONNECTING, wm.getState());

    // Back to WAITING
    accessor.test_simulateDisconnect(WIFI_REASON_BEACON_TIMEOUT);
    vTaskDelay(pdMS_TO_TICKS(100));

    printf("Testing DISCONNECT in WAITING_RECONNECT...\n");
    wm.disconnect(); // Async
    vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_EQUAL(WiFiManager::State::DISCONNECTED, wm.getState());

    wm.deinit();
}

#endif // UNIT_TEST
