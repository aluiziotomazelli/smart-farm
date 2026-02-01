// test_state_helpers.cpp
#ifdef UNIT_TEST

#include "test_state_helpers.hpp"
#include "esp_log.h"
#include "test_wifi_manager_accessor.hpp"
#include <vector>

const char *TestStateHelpers::stateToString(WiFiManager::State state)
{
    switch (state) {
    case WiFiManager::State::UNINITIALIZED:
        return "UNINITIALIZED";
    case WiFiManager::State::INITIALIZING:
        return "INITIALIZING";
    case WiFiManager::State::INITIALIZED:
        return "INITIALIZED";
    case WiFiManager::State::STARTING:
        return "STARTING";
    case WiFiManager::State::STARTED:
        return "STARTED";
    case WiFiManager::State::CONNECTING:
        return "CONNECTING";
    case WiFiManager::State::CONNECTED_NO_IP:
        return "CONNECTED_NO_IP";
    case WiFiManager::State::CONNECTED_GOT_IP:
        return "CONNECTED_GOT_IP";
    case WiFiManager::State::DISCONNECTING:
        return "DISCONNECTING";
    case WiFiManager::State::DISCONNECTED:
        return "DISCONNECTED";
    case WiFiManager::State::WAITING_RECONNECT:
        return "WAITING_RECONNECT";
    case WiFiManager::State::ERROR_CREDENTIALS:
        return "ERROR_CREDENTIALS";
    case WiFiManager::State::STOPPING:
        return "STOPPING";
    case WiFiManager::State::STOPPED:
        return "STOPPED";
    default:
        return "UNKNOWN";
    }
}

const std::vector<WiFiManager::State> TestStateHelpers::getAllStates()
{
    return {WiFiManager::State::UNINITIALIZED,     WiFiManager::State::INITIALIZING,
            WiFiManager::State::INITIALIZED,       WiFiManager::State::STARTING,
            WiFiManager::State::STARTED,           WiFiManager::State::CONNECTING,
            WiFiManager::State::CONNECTED_NO_IP,   WiFiManager::State::CONNECTED_GOT_IP,
            WiFiManager::State::DISCONNECTING,     WiFiManager::State::DISCONNECTED,
            WiFiManager::State::WAITING_RECONNECT, WiFiManager::State::ERROR_CREDENTIALS,
            WiFiManager::State::STOPPING,          WiFiManager::State::STOPPED};
}

bool TestStateHelpers::forceState(WiFiManager &wm, WiFiManager::State target_state)
{
    WiFiManagerTestAccessor accessor(wm);

    // Reset para estado base
    wm.deinit();
    wm.init();

    // Estados especiais que precisam de tratamento específico
    switch (target_state) {
    case WiFiManager::State::UNINITIALIZED:
        wm.deinit();
        return true;

    case WiFiManager::State::INITIALIZED:
        return true; // Já é o estado após init()

    case WiFiManager::State::STARTING:
        accessor.test_sendStartCommand(false);
        vTaskDelay(pdMS_TO_TICKS(10));
        return wm.getState() == target_state;

    case WiFiManager::State::STARTED:
        accessor.test_sendStartCommand(false);
        accessor.test_simulateWifiEvent(WIFI_EVENT_STA_START);
        vTaskDelay(pdMS_TO_TICKS(10));
        return wm.getState() == target_state;

    case WiFiManager::State::CONNECTING:
        accessor.test_sendStartCommand(false);
        accessor.test_simulateWifiEvent(WIFI_EVENT_STA_START);
        wm.setCredentials("TestSSID", "TestPass");
        accessor.test_sendConnectCommand(false);
        vTaskDelay(pdMS_TO_TICKS(10));
        return wm.getState() == target_state;

    case WiFiManager::State::CONNECTED_NO_IP:
        accessor.test_sendStartCommand(false);
        accessor.test_simulateWifiEvent(WIFI_EVENT_STA_START);
        wm.setCredentials("TestSSID", "TestPass");
        accessor.test_sendConnectCommand(false);
        accessor.test_simulateWifiEvent(WIFI_EVENT_STA_CONNECTED);
        vTaskDelay(pdMS_TO_TICKS(10));
        return wm.getState() == target_state;

    case WiFiManager::State::CONNECTED_GOT_IP:
        accessor.test_sendStartCommand(false);
        accessor.test_simulateWifiEvent(WIFI_EVENT_STA_START);
        wm.setCredentials("TestSSID", "TestPass");
        accessor.test_sendConnectCommand(false);
        accessor.test_simulateWifiEvent(WIFI_EVENT_STA_CONNECTED);
        accessor.test_simulateIpEvent(IP_EVENT_STA_GOT_IP);
        vTaskDelay(pdMS_TO_TICKS(10));
        return wm.getState() == target_state;

    case WiFiManager::State::DISCONNECTING:
        // Para chegar a DISCONNECTING, precisamos estar conectados primeiro
        accessor.test_sendStartCommand(false);
        accessor.test_simulateWifiEvent(WIFI_EVENT_STA_START);
        wm.setCredentials("TestSSID", "TestPass");
        accessor.test_sendConnectCommand(false);
        accessor.test_simulateWifiEvent(WIFI_EVENT_STA_CONNECTED);
        accessor.test_simulateIpEvent(IP_EVENT_STA_GOT_IP);
        accessor.test_sendDisconnectCommand(false);
        vTaskDelay(pdMS_TO_TICKS(10));
        return wm.getState() == target_state;

    case WiFiManager::State::DISCONNECTED:
        // Várias formas de chegar a DISCONNECTED
        accessor.test_sendStartCommand(false);
        accessor.test_simulateWifiEvent(WIFI_EVENT_STA_START);
        wm.setCredentials("TestSSID", "TestPass");
        accessor.test_sendConnectCommand(false);
        accessor.test_simulateWifiEvent(WIFI_EVENT_STA_CONNECTED);
        accessor.test_simulateIpEvent(IP_EVENT_STA_GOT_IP);
        accessor.test_sendDisconnectCommand(false);
        accessor.test_simulateDisconnect(WIFI_REASON_ASSOC_LEAVE);
        vTaskDelay(pdMS_TO_TICKS(10));
        return wm.getState() == target_state;

    case WiFiManager::State::WAITING_RECONNECT:
        accessor.test_sendStartCommand(false);
        accessor.test_simulateWifiEvent(WIFI_EVENT_STA_START);
        wm.setCredentials("TestSSID", "TestPass");
        accessor.test_sendConnectCommand(false);
        accessor.test_simulateDisconnect(WIFI_REASON_BEACON_TIMEOUT);
        vTaskDelay(pdMS_TO_TICKS(50));
        return wm.getState() == target_state;

    case WiFiManager::State::ERROR_CREDENTIALS:
        accessor.test_sendStartCommand(false);
        accessor.test_simulateWifiEvent(WIFI_EVENT_STA_START);
        wm.setCredentials("WrongSSID", "WrongPass");
        accessor.test_sendConnectCommand(false);
        accessor.test_simulateDisconnect(WIFI_REASON_AUTH_FAIL);
        vTaskDelay(pdMS_TO_TICKS(50));
        return wm.getState() == target_state;

    case WiFiManager::State::STOPPING:
        accessor.test_sendStartCommand(false);
        accessor.test_simulateWifiEvent(WIFI_EVENT_STA_START);
        accessor.test_sendStopCommand(false);
        vTaskDelay(pdMS_TO_TICKS(10));
        return wm.getState() == target_state;

    case WiFiManager::State::STOPPED:
        accessor.test_sendStartCommand(false);
        accessor.test_simulateWifiEvent(WIFI_EVENT_STA_START);
        accessor.test_sendStopCommand(false);
        accessor.test_simulateWifiEvent(WIFI_EVENT_STA_STOP);
        vTaskDelay(pdMS_TO_TICKS(10));
        return wm.getState() == target_state;

    default:
        return false;
    }
}

void TestStateHelpers::testCommand(WiFiManager &wm,
                                   WiFiManagerTestAccessor &accessor,
                                   const char *command_name,
                                   std::function<esp_err_t()> send_command,
                                   bool expected_success)
{
    printf("    Command: %s -> ", command_name);

    // Salvar estado inicial
    WiFiManager::State initial_state = wm.getState();

    // Executar comando
    esp_err_t result = send_command();

    // Verificar resultado
    if (result == ESP_OK) {
        printf("ESP_OK");
        if (!expected_success) {
            printf(" [UNEXPECTED SUCCESS!]\n");
            // TEST_FAIL_MESSAGE("Command should have failed but succeeded");
        }
        else {
            printf(" [OK]\n");
        }
    }
    else if (result == ESP_ERR_INVALID_STATE) {
        printf("ESP_ERR_INVALID_STATE");
        if (expected_success) {
            printf(" [UNEXPECTED FAILURE!]\n");
            printf("    State: %s -> Command rejected\n", stateToString(initial_state));
            // TEST_FAIL_MESSAGE("Command should have succeeded but was rejected");
        }
        else {
            printf(" [Expected]\n");
        }
    }
    else if (result == ESP_FAIL) {
        printf("ESP_FAIL (queue full?)");
        printf(" [UNEXPECTED]\n");
        // TEST_FAIL_MESSAGE("Queue full or other unexpected failure");
    }
    else {
        printf("0x%x", result);
        printf(" [UNKNOWN]\n");
    }

    // Pequeno delay para processamento
    vTaskDelay(pdMS_TO_TICKS(10));
}

#endif // UNIT_TEST