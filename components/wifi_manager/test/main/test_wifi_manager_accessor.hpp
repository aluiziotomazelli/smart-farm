// test_wifi_accessor.hpp
#pragma once

#ifdef UNIT_TEST

#include "esp_wifi.h"
#include "wifi_manager.hpp"

class WiFiManagerTestAccessor
{
private:
    WiFiManager &wifi_manager_;

public:
    WiFiManagerTestAccessor(WiFiManager &wm)
        : wifi_manager_(wm)
    {
    }

    // === Interface de Teste via Helpers ===

    // Teste de comandos básicos
    esp_err_t test_sendStartCommand(bool is_async = true)
    {
        return wifi_manager_.testHelper_sendStartCommand(is_async);
    }

    esp_err_t test_sendStopCommand(bool is_async = true)
    {
        return wifi_manager_.testHelper_sendStopCommand(is_async);
    }

    esp_err_t test_sendConnectCommand(const std::string &ssid,
                                      const std::string &password,
                                      bool is_async = true)
    {
        return wifi_manager_.testHelper_sendConnectCommand(ssid, password, is_async);
    }

    esp_err_t test_sendDisconnectCommand(bool is_async = true)
    {
        return wifi_manager_.testHelper_sendDisconnectCommand(is_async);
    }

    // Teste de estado interno
    WiFiManager::State test_getInternalState()
    {
        return wifi_manager_.getState();
    }

    // Teste de fila
    uint32_t test_getQueuePendingCount()
    {
        return wifi_manager_.testHelper_getQueuePendingCount();
    }

    bool test_isQueueFull()
    {
        return wifi_manager_.testHelper_isQueueFull();
    }

    void test_simulateDisconnect(uint8_t reason)
    {
        wifi_event_sta_disconnected_t disconn = {};
        disconn.reason                        = reason;
        WiFiManager::wifiEventHandler(&wifi_manager_, WIFI_EVENT,
                                      WIFI_EVENT_STA_DISCONNECTED, &disconn);
    }

    uint32_t test_getQueueCapacity()
    {
        uint32_t pending = wifi_manager_.testHelper_getQueuePendingCount();
        uint32_t free    = wifi_manager_.testHelper_isQueueFull() ? 0 : 10 - pending;
        return pending + free; // Capacidade total = 10
    }
};

#endif // UNIT_TEST