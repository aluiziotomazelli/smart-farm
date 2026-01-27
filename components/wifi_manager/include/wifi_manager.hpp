#pragma once

#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include <cstdint>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/**
 * @class WiFiManager
 * @brief Singleton class for managing WiFi connections on ESP32.
 *
 * This class uses a dedicated FreeRTOS task to handle all WiFi operations,
 * ensuring thread safety and a non-blocking internal architecture.
 * It provides both synchronous and asynchronous methods for ease of use.
 */
class WiFiManager
{
public:
    /**
     * @brief Publicly accessible states of the WiFi manager.
     */
    enum class State
    {
        UNINITIALIZED,
        INITIALIZING,
        INITIALIZED,
        STARTING,
        STARTED,
        STOPPING,
        STOPPED,
        CONNECTING,
        CONNECTED_NO_IP,
        CONNECTED_GOT_IP,
        DISCONNECTING,
        DISCONNECTED,
    };

    /**
     * @brief Get the singleton instance of WiFiManager.
     * @return Reference to the WiFiManager instance.
     */
    static WiFiManager &instance();

    // Prevent copying and assignment
    WiFiManager(const WiFiManager &)            = delete;
    WiFiManager &operator=(const WiFiManager &) = delete;

    /**
     * @brief Initialize the WiFi stack, creates the event queue and the manager
     * task.
     * @return ESP_OK on success, error code on failure.
     */
    esp_err_t init();

    /**
     * @brief Deinitialize the WiFi stack, clean up resources.
     * @return ESP_OK on success, error code on failure.
     */
    esp_err_t deinit();

    /**
     * @brief Start the WiFi station mode (synchronous).
     * @param timeout_ms Maximum time to wait for the operation to complete.
     * @return ESP_OK on success, ESP_ERR_TIMEOUT on timeout, or other error code.
     */
    esp_err_t start(uint32_t timeout_ms = 5000);

    /**
     * @brief Stop the WiFi station mode (synchronous).
     * @param timeout_ms Maximum time to wait for the operation to complete.
     * @return ESP_OK on success, ESP_ERR_TIMEOUT on timeout, or other error code.
     */
    esp_err_t stop(uint32_t timeout_ms = 5000);

    /**
     * @brief Connect to a WiFi network (synchronous).
     * @param ssid The network SSID.
     * @param password The network password.
     * @param timeout_ms Maximum time to wait for connection and IP.
     * @return ESP_OK on success, ESP_ERR_TIMEOUT on timeout, or other error code.
     */
    esp_err_t connect(const std::string &ssid,
                      const std::string &password,
                      uint32_t timeout_ms);

    /**
     * @brief Connect to a WiFi network (asynchronous).
     * @param ssid The network SSID.
     * @param password The network password.
     * @return ESP_OK if the command was sent successfully, error code otherwise.
     */
    esp_err_t connect_async(const std::string &ssid, const std::string &password);

    /**
     * @brief Disconnect from the current WiFi network (synchronous).
     * @param timeout_ms Maximum time to wait for the operation to complete.
     * @return ESP_OK on success, ESP_ERR_TIMEOUT on timeout, or other error code.
     */
    esp_err_t disconnect(uint32_t timeout_ms = 5000);

    /**
     * @brief Get the current state of the WiFi manager.
     * @return The current State enum value.
     */
    State getState() const;

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
    WiFiManager();
    ~WiFiManager();

    esp_err_t init_nvs();

    // --- Internal Machinery ---

    // Command structure for the queue
    enum class CommandId
    {
        START,
        STOP,
        CONNECT,
        DISCONNECT,
        HANDLE_EVENT_WIFI,
        HANDLE_EVENT_IP,
        EXIT,
    };

    struct Command
    {
        CommandId id;
        // For CONNECT
        std::string ssid;
        std::string password;
        // For event handling
        int32_t event_id;
    };

    // Event bits for synchronization
    static constexpr EventBits_t STARTED_BIT        = BIT0;
    static constexpr EventBits_t STOPPED_BIT        = BIT1;
    static constexpr EventBits_t CONNECTED_BIT      = BIT2;
    static constexpr EventBits_t DISCONNECTED_BIT   = BIT3;
    static constexpr EventBits_t CONNECT_FAILED_BIT = BIT4;
    static constexpr EventBits_t ALL_SYNC_BITS      = STARTED_BIT | STOPPED_BIT |
                                                 CONNECTED_BIT | DISCONNECTED_BIT |
                                                 CONNECT_FAILED_BIT;

    // Task and event handlers
    static void wifiTask(void *pvParameters);

    esp_event_handler_instance_t wifi_event_instance_;
    esp_event_handler_instance_t ip_event_instance_;
    esp_netif_t *sta_netif_ptr_;

    static void wifiEventHandler(void *arg,
                                 esp_event_base_t base,
                                 int32_t id,
                                 void *data);
    static void ipEventHandler(void *arg,
                               esp_event_base_t base,
                               int32_t id,
                               void *data);

    // Helper to send a command to the queue
    esp_err_t sendCommand(const Command &cmd, bool is_async);

    // RTOS objects
    TaskHandle_t task_handle_;
    QueueHandle_t command_queue_;
    EventGroupHandle_t wifi_event_group_;
    mutable SemaphoreHandle_t state_mutex_; // Protects current_state_

    // State variable
    State current_state_;
};