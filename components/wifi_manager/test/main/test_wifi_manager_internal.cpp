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
    wm.disconnect(1000); // Async call
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

TEST_CASE("test_command_validation_strict", "[wifi][state][strict]")
{
    printf("\n=== Test: Command Validation (Strict) ===\n");

    WiFiManager &wm = WiFiManager::instance();
    WiFiManagerTestAccessor accessor(wm);

    // Teste 1: Comandos em UNINITIALIZED
    printf("\nTest 1: Commands in UNINITIALIZED state\n");
    wm.deinit();

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, accessor.test_sendStartCommand(true));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, accessor.test_sendStopCommand(true));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, accessor.test_sendConnectCommand(true));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, accessor.test_sendDisconnectCommand(true));

    // Teste 2: Comandos em INITIALIZED
    printf("\nTest 2: Commands in INITIALIZED state\n");
    wm.init();

    // START deve funcionar
    TEST_ASSERT_EQUAL(ESP_OK, accessor.test_sendStartCommand(true));

    // STOP não deve funcionar (não está started)
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, accessor.test_sendStopCommand(true));

    // Dar tempo para processar START
    vTaskDelay(pdMS_TO_TICKS(100));

    // Teste 3: Comandos em STARTED
    printf("\nTest 3: Commands in STARTED state\n");
    accessor.test_simulateWifiEvent(WIFI_EVENT_STA_START);
    vTaskDelay(pdMS_TO_TICKS(50));

    // START novamente deve funcionar (já está started)
    TEST_ASSERT_EQUAL(ESP_OK, accessor.test_sendStartCommand(true));

    // STOP deve funcionar
    TEST_ASSERT_EQUAL(ESP_OK, accessor.test_sendStopCommand(true));

    // CONNECT sem credenciais
    TEST_ASSERT_EQUAL(ESP_OK, accessor.test_sendConnectCommand(true));

    // Teste 4: Comandos em CONNECTED_GOT_IP
    printf("\nTest 4: Commands in CONNECTED_GOT_IP state\n");

    // Reset e ir para CONNECTED_GOT_IP
    wm.deinit();
    wm.init();
    accessor.test_sendStartCommand(false);
    accessor.test_simulateWifiEvent(WIFI_EVENT_STA_START);
    wm.setCredentials("TestSSID", "TestPass");
    accessor.test_sendConnectCommand(false);
    accessor.test_simulateWifiEvent(WIFI_EVENT_STA_CONNECTED);
    accessor.test_simulateIpEvent(IP_EVENT_STA_GOT_IP);
    vTaskDelay(pdMS_TO_TICKS(100));

    printf("State: %d\n", static_cast<int>(wm.getState()));

    // CONNECT novamente deve funcionar (já está connected)
    TEST_ASSERT_EQUAL(ESP_OK, accessor.test_sendConnectCommand(true));

    // DISCONNECT deve funcionar
    TEST_ASSERT_EQUAL(ESP_OK, accessor.test_sendDisconnectCommand(true));

    // Simular desconexão
    accessor.test_simulateDisconnect(WIFI_REASON_ASSOC_LEAVE);
    vTaskDelay(pdMS_TO_TICKS(100));

    printf("Final state: %d\n", static_cast<int>(wm.getState()));

    wm.deinit();
}

TEST_CASE("test_specific_state_transitions", "[wifi][state][transitions]")
{
    printf("\n=== Test: Specific State Transitions ===\n");

    WiFiManager &wm = WiFiManager::instance();
    WiFiManagerTestAccessor accessor(wm);

    // Teste 1: INITIALIZED -> STARTING -> STARTED
    printf("\nTest 1: INITIALIZED to STARTED transition\n");
    wm.deinit();
    wm.init();

    TEST_ASSERT_EQUAL(static_cast<int>(WiFiManager::State::INITIALIZED),
                      static_cast<int>(wm.getState()));

    // Enviar START
    TEST_ASSERT_EQUAL(ESP_OK, accessor.test_sendStartCommand(false));
    vTaskDelay(pdMS_TO_TICKS(10));

    // Deve estar em STARTING
    printf("State after START command: %d\n", static_cast<int>(wm.getState()));

    // Simular STA_START
    accessor.test_simulateWifiEvent(WIFI_EVENT_STA_START);
    vTaskDelay(pdMS_TO_TICKS(50));

    TEST_ASSERT_EQUAL(static_cast<int>(WiFiManager::State::STARTED),
                      static_cast<int>(wm.getState()));

    // Teste 2: STARTED -> CONNECTING -> CONNECTED_GOT_IP
    printf("\nTest 2: STARTED to CONNECTED_GOT_IP transition\n");
    wm.setCredentials("TestSSID", "TestPass");

    TEST_ASSERT_EQUAL(ESP_OK, accessor.test_sendConnectCommand(false));
    vTaskDelay(pdMS_TO_TICKS(10));

    printf("State after CONNECT command: %d\n", static_cast<int>(wm.getState()));

    accessor.test_simulateWifiEvent(WIFI_EVENT_STA_CONNECTED);
    vTaskDelay(pdMS_TO_TICKS(10));

    printf("State after STA_CONNECTED: %d\n", static_cast<int>(wm.getState()));

    accessor.test_simulateIpEvent(IP_EVENT_STA_GOT_IP);
    vTaskDelay(pdMS_TO_TICKS(50));

    TEST_ASSERT_EQUAL(static_cast<int>(WiFiManager::State::CONNECTED_GOT_IP),
                      static_cast<int>(wm.getState()));

    // Teste 3: CONNECTED_GOT_IP -> DISCONNECTING -> DISCONNECTED
    printf("\nTest 3: CONNECTED_GOT_IP to DISCONNECTED transition\n");

    TEST_ASSERT_EQUAL(ESP_OK, accessor.test_sendDisconnectCommand(false));
    vTaskDelay(pdMS_TO_TICKS(10));

    printf("State after DISCONNECT command: %d\n", static_cast<int>(wm.getState()));

    accessor.test_simulateDisconnect(WIFI_REASON_ASSOC_LEAVE);
    vTaskDelay(pdMS_TO_TICKS(50));

    printf("State after disconnect event: %d\n", static_cast<int>(wm.getState()));

    // Teste 4: DISCONNECTED -> STOPPING -> STOPPED
    printf("\nTest 4: DISCONNECTED to STOPPED transition\n");

    TEST_ASSERT_EQUAL(ESP_OK, accessor.test_sendStopCommand(false));
    vTaskDelay(pdMS_TO_TICKS(10));

    printf("State after STOP command: %d\n", static_cast<int>(wm.getState()));

    accessor.test_simulateWifiEvent(WIFI_EVENT_STA_STOP);
    vTaskDelay(pdMS_TO_TICKS(50));

    TEST_ASSERT_EQUAL(static_cast<int>(WiFiManager::State::STOPPED),
                      static_cast<int>(wm.getState()));

    wm.deinit();
}

#endif // UNIT_TEST
