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
// These tests use the TestAccessor to simulate driver events and verify
// the state machine logic without requiring a real Access Point.
// ========================================================================

#ifdef UNIT_TEST

/**
 * @brief Test: Queue capacity and behavior
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
    int successful_sends = 0;
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
 * @brief Test: Full connection flow simulation
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
    wm.start(); // Async
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(WiFiManager::State::STARTING, wm.getState());

    accessor.test_simulateWifiEvent(WIFI_EVENT_STA_START);
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(WiFiManager::State::STARTED, wm.getState());

    // 2. Connect
    wm.setCredentials("SimulatedSSID", "SimulatedPass");
    wm.connect(); // Async
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(WiFiManager::State::CONNECTING, wm.getState());

    accessor.test_simulateWifiEvent(WIFI_EVENT_STA_CONNECTED);
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(WiFiManager::State::CONNECTED_NO_IP, wm.getState());

    accessor.test_simulateIpEvent(IP_EVENT_STA_GOT_IP);
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(WiFiManager::State::CONNECTED_GOT_IP, wm.getState());

    wm.deinit();
}

/**
 * @brief Test: Auto-reconnect on loss
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
 * @brief Test: Immediate invalidation logic
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
 * @brief Test: Suspect failure 3-strike logic
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
 * @brief Test: Manual interrupt during backoff
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
    wm.disconnect(); // Async call
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL(WiFiManager::State::DISCONNECTED, wm.getState());

    wm.deinit();
}

#endif // UNIT_TEST
