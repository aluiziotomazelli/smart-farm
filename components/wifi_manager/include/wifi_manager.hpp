#pragma once

/**
 * @file wifi_manager.hpp
 * @brief Singleton WiFi Manager for ESP32.
 * @author Jules
 * @version 1.0.0
 */

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

// Forward declaration for tests
#ifdef UNIT_TEST
class WiFiManagerTestAccessor;
#endif

/**
 * @class WiFiManager
 * @brief Singleton class for managing WiFi connections on ESP32.
 *
 * This class uses a dedicated FreeRTOS task to handle all WiFi operations,
 * ensuring thread safety and a non-blocking internal architecture.
 * It provides both synchronous (blocking) and asynchronous (non-blocking) methods.
 */
class WiFiManager
{
#ifdef UNIT_TEST
    friend class WiFiManagerTestAccessor;
#endif

public:
    /**
     * @brief Internal states of the WiFi manager.
     */
    enum class State
    {
        UNINITIALIZED,    ///< Initial state before init() is called.
        INITIALIZING,     ///< In the process of setting up resources.
        INITIALIZED,      ///< Resources allocated, task running, driver not started.
        STARTING,         ///< In the process of starting the WiFi driver.
        STARTED,          ///< WiFi driver started in STA mode.
        CONNECTING,       ///< Attempting to connect to an AP.
        CONNECTED_NO_IP,  ///< Connected to AP, waiting for DHCP/Static IP.
        CONNECTED_GOT_IP, ///< Successfully connected and has an IP address.
        DISCONNECTING,    ///< In the process of disconnecting from the AP.
        DISCONNECTED,     ///< Not connected to any AP.
        STOPPING,         ///< In the process of stopping the WiFi driver.
        STOPPED,          ///< WiFi driver stopped.
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
     * @brief Initialize the WiFi stack.
     *
     * Initializes NVS, Netif, Event Loop, creates the command queue,
     * event group, and launches the internal manager task.
     *
     * @return
     *  - ESP_OK: Success.
     *  - ESP_ERR_NO_MEM: Failed to allocate RTOS resources.
     *  - Others: Failed to initialize system-level components.
     */
    esp_err_t init();

    /**
     * @brief Deinitialize the WiFi stack.
     *
     * Stops the WiFi driver if running, terminates the manager task,
     * and releases all allocated RTOS and system resources.
     *
     * @return ESP_OK on success.
     */
    esp_err_t deinit();

    /**
     * @brief Start the WiFi station mode (synchronous).
     *
     * Blocks until the WiFi driver is successfully started or a timeout occurs.
     *
     * @param timeout_ms Maximum time to wait for the operation to complete.
     * @return
     *  - ESP_OK: Started successfully.
     *  - ESP_ERR_TIMEOUT: Operation timed out.
     *  - ESP_ERR_INVALID_STATE: Manager is not initialized.
     */
    esp_err_t start(uint32_t timeout_ms = 5000);

    /**
     * @brief Start the WiFi station mode (asynchronous).
     *
     * Returns immediately after queuing the start command.
     *
     * @return ESP_OK if the command was sent successfully.
     */
    esp_err_t start_async();

    /**
     * @brief Stop the WiFi station mode (synchronous).
     *
     * Blocks until the WiFi driver is stopped or a timeout occurs.
     *
     * @param timeout_ms Maximum time to wait for the operation to complete.
     * @return ESP_OK on success, ESP_ERR_TIMEOUT on timeout.
     */
    esp_err_t stop(uint32_t timeout_ms = 5000);

    /**
     * @brief Stop the WiFi station mode (asynchronous).
     *
     * Returns immediately after queuing the stop command.
     *
     * @return ESP_OK if the command was sent successfully.
     */
    esp_err_t stop_async();

    /**
     * @brief Connect to a WiFi network (synchronous).
     *
     * Blocks until a connection is established and an IP is obtained,
     * or a timeout/error occurs.
     *
     * @param ssid The network SSID.
     * @param password The network password.
     * @param timeout_ms Maximum time to wait for connection and IP.
     * @return
     *  - ESP_OK: Connected and has IP.
     *  - ESP_ERR_TIMEOUT: Failed to connect within the time limit.
     *  - ESP_FAIL: Driver reported immediate failure.
     */
    esp_err_t connect(const std::string &ssid,
                      const std::string &password,
                      uint32_t timeout_ms);

    /**
     * @brief Connect to a WiFi network (asynchronous).
     *
     * Returns immediately after queuing the connect command.
     *
     * @param ssid The network SSID.
     * @param password The network password.
     * @return ESP_OK if the command was sent successfully.
     */
    esp_err_t connect_async(const std::string &ssid, const std::string &password);

    /**
     * @brief Disconnect from the current WiFi network (synchronous).
     *
     * Blocks until the disconnection is confirmed by the driver.
     *
     * @param timeout_ms Maximum time to wait for the operation to complete.
     * @return ESP_OK on success.
     */
    esp_err_t disconnect(uint32_t timeout_ms = 5000);

    /**
     * @brief Disconnect from the current WiFi network (asynchronous).
     *
     * Returns immediately after queuing the disconnect command.
     *
     * @return ESP_OK if the command was sent successfully.
     */
    esp_err_t disconnect_async();

    /**
     * @brief Get the current state of the WiFi manager.
     * @return The current State enum value.
     */
    State getState() const;

    /**
     * @brief Store WiFi credentials in NVS.
     *
     * Uses a dedicated NVS namespace "wifi_manager".
     *
     * @param ssid The network SSID to store.
     * @param password The network password to store.
     * @return ESP_OK on success.
     */
    esp_err_t storeCredentials(const std::string &ssid, const std::string &password);

    /**
     * @brief Load WiFi credentials from NVS.
     *
     * @param ssid Output parameter for the loaded SSID.
     * @param password Output parameter for the loaded password.
     * @return ESP_OK on success, or error if not found.
     */
    esp_err_t loadCredentials(std::string &ssid, std::string &password);

    /**
     * @brief Check if credentials exist in NVS.
     * @return true if credentials are found and valid.
     */
    bool hasCredentials();

private:
    // Private constructor for singleton
    WiFiManager();
    // Private destructor
    ~WiFiManager();

    // Internal helper to initialize NVS flash partition
    esp_err_t init_nvs();

    // --- Internal Machinery ---

    // Internal command IDs for the manager task queue
    enum class CommandId
    {
        START,             // Request to start WiFi driver
        STOP,              // Request to stop WiFi driver
        CONNECT,           // Request to connect to an AP
        DISCONNECT,        // Request to disconnect from an AP
        HANDLE_EVENT_WIFI, // Bridge for WiFi system events
        HANDLE_EVENT_IP,   // Bridge for IP system events
        EXIT,              // Request to terminate the manager task
    };

    // Structure used to pass commands and data to the internal task
    struct Command
    {
        CommandId id;         // The operation requested
        std::string ssid;     // SSID for CONNECT commands
        std::string password; // Password for CONNECT commands
        int32_t event_id;     // Event ID for HANDLE_EVENT_* commands
    };

private:
    // FreeRTOS Event Group bits for synchronization between the API and the task
    static constexpr EventBits_t STARTED_BIT        = BIT0; // WiFi driver started
    static constexpr EventBits_t STOPPED_BIT        = BIT1; // WiFi driver stopped
    static constexpr EventBits_t CONNECTED_BIT      = BIT2; // Got IP address
    static constexpr EventBits_t DISCONNECTED_BIT   = BIT3; // Disconnected from AP
    static constexpr EventBits_t CONNECT_FAILED_BIT = BIT4; // Connection attempt failed
    static constexpr EventBits_t START_FAILED_BIT   = BIT5; // Driver start failed
    static constexpr EventBits_t STOP_FAILED_BIT    = BIT6; // Driver stop failed

    // Mask for all synchronization bits
    static constexpr EventBits_t ALL_SYNC_BITS =
        STARTED_BIT | STOPPED_BIT | CONNECTED_BIT | DISCONNECTED_BIT |
        CONNECT_FAILED_BIT | START_FAILED_BIT | STOP_FAILED_BIT;

    // Main FreeRTOS task loop that executes driver operations
    static void wifiTask(void *pvParameters);

    // Opaque handles for ESP-IDF event handler registrations
    esp_event_handler_instance_t wifi_event_instance_;
    esp_event_handler_instance_t ip_event_instance_;

    // Pointer to the default ESP-IDF station network interface
    esp_netif_t *sta_netif_ptr_;

    // Static callback for WiFi system events (bridged to task)
    static void wifiEventHandler(void *arg,
                                 esp_event_base_t base,
                                 int32_t id,
                                 void *data);

    // Static callback for IP system events (bridged to task)
    static void ipEventHandler(void *arg,
                               esp_event_base_t base,
                               int32_t id,
                               void *data);

    // Private helper to post commands to the internal queue
    esp_err_t sendCommand(const Command &cmd, bool is_async);

private:
    // FreeRTOS Task handle for the manager loop
    TaskHandle_t task_handle_;

    // FreeRTOS Queue handle for command passing
    QueueHandle_t command_queue_;

    // FreeRTOS Event Group handle for API synchronization
    EventGroupHandle_t wifi_event_group_;

    // Mutex to protect 'current_state_' access across tasks
    mutable SemaphoreHandle_t state_mutex_;

    // The current thread-protected internal state
    State current_state_;

#ifdef UNIT_TEST
    friend class WiFiManagerTestAccessor;

    // Helpers que criam e enviam comandos específicos
    esp_err_t testHelper_sendStartCommand(bool is_async);
    esp_err_t testHelper_sendStopCommand(bool is_async);
    esp_err_t testHelper_sendConnectCommand(const std::string &ssid,
                                            const std::string &password,
                                            bool is_async);
    esp_err_t testHelper_sendDisconnectCommand(bool is_async);

    // Helper para verificar fila (opcional)
    uint32_t testHelper_getQueuePendingCount() const;
    bool testHelper_isQueueFull() const;
#endif
};