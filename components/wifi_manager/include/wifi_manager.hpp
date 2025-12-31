#pragma once

#include "esp_err.h"
#include <cstdint>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/**
 * @class WiFiManager
 * @brief A singleton component to manage the ESP32's Wi-Fi lifecycle.
 *
 * This class handles the low-level initialization, starting/stopping of the Wi-Fi radio,
 * connecting to an access point, and managing credentials in NVS. It ensures that
 * initialization routines that should only run once are properly handled.
 */
class WiFiManager
{
public:
    /**
     * @brief Get the singleton instance of the WiFiManager.
     * @return A pointer to the WiFiManager instance.
     */
    static WiFiManager *getInstance();

    // Delete copy constructor and assignment operator for singleton pattern.
    WiFiManager(const WiFiManager &)            = delete;
    WiFiManager &operator=(const WiFiManager &) = delete;

    /**
     * @brief Performs one-time initialization of the underlying network stack.
     * This includes esp_netif_init, esp_event_loop_create_default, and esp_wifi_init.
     * This method is safe to call multiple times.
     * @return ESP_OK on success, or an error code from the underlying ESP-IDF functions.
     */
    esp_err_t init();

    /**
     * @brief Starts the Wi-Fi driver and radio.
     * Must be called after init().
     * @return ESP_OK on success, or an error code from esp_wifi_start.
     */
    esp_err_t start();

    /**
     * @brief Stops the Wi-Fi driver and radio.
     * @return ESP_OK on success, or an error code from esp_wifi_stop.
     */
    esp_err_t stop();

    /**
     * @brief Connects to a Wi-Fi access point with the given credentials.
     * The Wi-Fi driver must be started before calling this method.
     * @param ssid The SSID of the access point.
     * @param password The password for the access point.
     * @param timeout_ms Timeout in milliseconds to wait for an IP address.
     * @return ESP_OK on successful connection, ESP_ERR_TIMEOUT on timeout, or another error code on failure.
     */
    esp_err_t connect(const std::string &ssid,
                      const std::string &password,
                      uint32_t timeout_ms = 15000);

    /**
     * @brief Disconnects from the currently connected Wi-Fi access point.
     * @return ESP_OK on success, or an error code from esp_wifi_disconnect.
     */
    esp_err_t disconnect();

    /**
     * @brief Stores Wi-Fi credentials in NVS.
     * @param ssid The SSID to store.
     * @param password The password to store.
     * @return ESP_OK on success, or an NVS error code on failure.
     */
    esp_err_t storeCredentials(const std::string &ssid, const std::string &password);

    /**
     * @brief Loads Wi-Fi credentials from NVS.
     * @param[out] ssid The loaded SSID.
     * @param[out] password The loaded password.
     * @return ESP_OK on success, or an NVS error code if not found or on failure.
     */
    esp_err_t loadCredentials(std::string &ssid, std::string &password);

    /**
     * @brief Checks if any credentials are stored in NVS.
     * @return True if credentials exist, false otherwise.
     */
    bool hasCredentials();

private:
    WiFiManager();
    ~WiFiManager();

    bool waitForIp(uint32_t timeout_ms);

    // Singleton instance
    static WiFiManager *instance_;
    static SemaphoreHandle_t instance_mutex_;

    // State flags
    bool initialized_;
    bool started_;
    bool connected_;

    static const char *TAG;
};
