#pragma once

#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/**
 * @class WifiManager
 * @brief Manages WiFi connectivity for ESP32 applications
 *
 * This class provides an abstraction layer for initializing and managing
 * WiFi connections on ESP32 devices. It handles connection events,
 * IP address acquisition, and provides a task-based architecture for
 * asynchronous WiFi management.
 */
class WifiManager
{
public:
    /**
     * @brief Get the singleton instance of WifiManager
     *
     * Provides access to the single, globally available instance
     * of the WifiManager class. Implements the singleton pattern
     * to ensure only one WiFi manager exists in the application.
     *
     * @return WifiManager& Reference to the singleton instance
     *
     * @note The first call to this function will construct the instance.
     *       Subsequent calls will return the same instance.
     * @warning This is not thread-safe on first initialization.
     *          Consider calling this early in the main thread if
     *          multi-threaded access is possible during startup.
     */
    static WifiManager &instance();

    /**
     * @brief Initialize the WiFi manager
     *
     * Sets up necessary data structures and prepares the WiFi manager
     * for operation. Must be called before start().
     *
     * @return esp_err_t ESP_OK on success, error code on failure
     *
     * @note This function does not actually start WiFi connection,
     *       use start() after successful initialization.
     */
    esp_err_t init();

    /**
     * @brief Start the WiFi manager
     *
     * Creates the WiFi management task and begins the connection process.
     * The WiFi manager will attempt to connect to configured networks
     * and handle connection events asynchronously.
     *
     * @pre init() must be called successfully before calling this function
     */
    void start();

private:
    /**
     * @brief Default constructor (private for singleton pattern)
     *
     * The constructor is private and defaulted to enforce the singleton
     * pattern. This ensures that the class can only be instantiated
     * through the instance() method, preventing multiple instances.
     *
     * The default constructor initializes all member variables to
     * their default values (nullptr for pointers/Handles).
     *
     * @note Using `= default` allows the compiler to generate the
     *       trivial constructor while keeping it private.
     */
    WifiManager() = default;

    /**
     * @brief WiFi management task function
     *
     * Static task function that handles WiFi connection management.
     * This task runs independently and processes WiFi commands and events.
     *
     * @param arg Pointer to WifiManager instance (passed as this pointer)
     */
    static void task(void *arg);

    /**
     * @brief WiFi command event handler
     *
     * Handles WiFi command events such as connection requests,
     * disconnection requests, and other WiFi control commands.
     *
     * @param arg User argument (typically WifiManager instance)
     * @param base Event base identifier
     * @param id Event ID
     * @param data Event-specific data
     */
    static void cmd_event_handler(void *arg,
                                  esp_event_base_t base,
                                  int32_t id,
                                  void *data);

    /**
     * @brief IP event handler
     *
     * Handles IP-related events including IP address acquisition,
     * DHCP events, and network interface changes.
     *
     * @param arg User argument (typically WifiManager instance)
     * @param base Event base identifier
     * @param id Event ID
     * @param data Event-specific data
     */
    static void ip_event_handler(void *arg,
                                 esp_event_base_t base,
                                 int32_t id,
                                 void *data);

    /**
     * @brief General WiFi event handler
     *
     * Handles general WiFi events including connection status changes,
     * authentication events, and scan results.
     *
     * @param arg User argument (typically WifiManager instance)
     * @param base Event base identifier
     * @param id Event ID
     * @param data Event-specific data
     */
    static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data);

    TaskHandle_t task_handle_{nullptr};     /**< Handle to the WiFi management task */
    QueueHandle_t cmd_queue_{nullptr};      /**< Queue for WiFi command messages */
    SemaphoreHandle_t init_mutex_{nullptr}; /**< Mutex for thread-safe initialization */
    bool initialized_{false};               /**< Flag indicating initialization status */
};