#pragma once

#include "esp_err.h"
#include "esp_event.h"
#include <cstdint>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/**
 * @class WiFiManager
 * @brief Singleton class for managing WiFi connections on ESP32.
 *
 * This class provides a synchronous interface for WiFi operations while
 * internally using event-driven architecture for state management.
 * It handles connection, disconnection, and credential storage.
 */
class WiFiManager
{
public:
    /**
     * @brief Get the singleton instance of WiFiManager.
     * @return Reference to the WiFiManager instance.
     */
    static WiFiManager &instance();

    // Prevent copying and assignment
    WiFiManager(const WiFiManager &)            = delete;
    WiFiManager &operator=(const WiFiManager &) = delete;

    /**
     * @brief Initialize the WiFi stack and event handlers.
     * @return ESP_OK on success, error code on failure.
     */
    esp_err_t init();

    /**
     * @brief Start the WiFi station mode.
     * @return ESP_OK on success, error code on failure.
     * @note Waits for WIFI_EVENT_STA_START event confirmation.
     */
    esp_err_t start();

    /**
     * @brief Stop the WiFi station mode.
     * @return ESP_OK on success, error code on failure.
     * @note Waits for WIFI_EVENT_STA_STOP event confirmation.
     */
    esp_err_t stop();

    /**
     * @brief Connect to a WiFi network.
     * @param ssid The network SSID.
     * @param password The network password.
     * @param timeout_ms Maximum time to wait for connection (default: 15000ms).
     * @return ESP_OK on success, error code on failure.
     * @note If already connected, will disconnect first before reconnecting.
     */
    esp_err_t connect(const std::string &ssid,
                      const std::string &password,
                      uint32_t timeout_ms = 15000);

    /**
     * @brief Disconnect from the current WiFi network.
     * @return ESP_OK on success, error code on failure.
     * @note Waits for WIFI_EVENT_STA_DISCONNECTED event confirmation.
     */
    esp_err_t disconnect();

    /**
     * @brief Check if WiFi is currently connected to an access point.
     * @return true if connected, false otherwise.
     * @note Thread-safe. Reflects WIFI_EVENT_STA_CONNECTED/DISCONNECTED events.
     */
    bool isConnected() const;

    /**
     * @brief Check if WiFi station mode is started.
     * @return true if started, false otherwise.
     * @note Thread-safe. Reflects WIFI_EVENT_STA_START/STOP events.
     */
    bool isStarted() const;

    /**
     * @brief Store WiFi credentials in NVS.
     * @param ssid The network SSID to store.
     * @param password The network password to store.
     * @return ESP_OK on success, error code on failure.
     */
    esp_err_t storeCredentials(const std::string &ssid, const std::string &password);

    /**
     * @brief Load WiFi credentials from NVS.
     * @param ssid Output parameter for the loaded SSID.
     * @param password Output parameter for the loaded password.
     * @return ESP_OK on success, error code on failure.
     */
    esp_err_t loadCredentials(std::string &ssid, std::string &password);

    /**
     * @brief Check if credentials are stored in NVS.
     * @return true if credentials exist, false otherwise.
     */
    bool hasCredentials();

private:
    /**
     * @brief Private constructor for singleton pattern.
     */
    WiFiManager();

    /**
     * @brief Destructor.
     */
    ~WiFiManager();

    /**
     * @brief Wait for IP address assignment.
     * @param timeout_ms Maximum time to wait.
     * @return true if IP was obtained, false on timeout.
     * @note Uses semaphore signaled by ipEventHandler on IP_EVENT_STA_GOT_IP.
     */
    bool waitForIp(uint32_t timeout_ms);

    /**
     * @brief Static handler for WiFi events.
     * @param arg Pointer to WiFiManager instance.
     * @param base Event base.
     * @param id Event ID.
     * @param data Event data.
     * @note Updates started_ and connected_ states based on events.
     */
    static void wifiEventHandler(void *arg,
                                 esp_event_base_t base,
                                 int32_t id,
                                 void *data);

    /**
     * @brief Static handler for IP events.
     * @param arg Pointer to WiFiManager instance.
     * @param base Event base.
     * @param id Event ID.
     * @param data Event data.
     * @note Signals ip_got_sem_ on IP_EVENT_STA_GOT_IP.
     */
    static void ipEventHandler(void *arg, esp_event_base_t base, int32_t id, void *data);

    // Synchronization primitives
    SemaphoreHandle_t ip_got_sem_;  ///< Semaphore for IP acquisition synchronization
    SemaphoreHandle_t state_mutex_; ///< Mutex for protecting state variables

    // State variables (protected by state_mutex_)
    bool initialized_; ///< Whether WiFi stack is initialized
    bool started_;   ///< Whether WiFi station is started (WIFI_EVENT_STA_START received)
    bool connected_; ///< Whether connected to AP (WIFI_EVENT_STA_CONNECTED received)
};