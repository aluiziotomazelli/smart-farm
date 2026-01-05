#pragma once

#include "esp_err.h"
#include <cstdint>
#include <functional>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/**
 * @struct OtaManagerCallbacks
 * @brief Defines callbacks for monitoring OTA events.
 *
 * Allows the application to receive notifications for key stages of the OTA process.
 */
struct OtaManagerCallbacks
{
    std::function<void()> onOtaStarted;      /**< Called when the OTA download begins. */
    std::function<void()> onOtaFinished;     /**< Called on success, just before restart. */
    std::function<void(esp_err_t)> onOtaFailed; /**< Called on any failure. */
};

/**
 * @class OtaManager
 * @brief Manages Over-The-Air (OTA) updates in a synchronous, on-demand manner.
 *
 * This singleton class provides a simple, blocking API to perform OTA updates.
 * It creates a dedicated task for each OTA attempt, ensuring that no resources
 * are consumed while idle. Callbacks can be registered for detailed status monitoring.
 */
class OtaManager
{
public:
    // Singleton access
    static OtaManager &instance();

    // Prevent copying
    OtaManager(const OtaManager &)            = delete;
    OtaManager &operator=(const OtaManager &) = delete;

    /**
     * @brief Performs basic initialization.
     * @return ESP_OK on success.
     */
    esp_err_t init();

    /**
     * @brief Registers callbacks for OTA events.
     * @param callbacks The struct containing the callback functions.
     */
    void registerCallbacks(const OtaManagerCallbacks &callbacks);

    /**
     * @brief Start an OTA update from a full URL (synchronous).
     *
     * This function blocks until the OTA process completes, fails, or times out.
     *
     * @param url The full URL of the firmware binary.
     * @param timeout_ms Timeout for the entire operation.
     * @return ESP_OK on success, or an error code on failure.
     */
    esp_err_t startOta(const std::string &url, uint32_t timeout_ms = 120000);

    /**
     * @brief Start an OTA update by resolving a server via mDNS (synchronous).
     *
     * This function blocks until the OTA process completes, fails, or times out.
     * It constructs the final URL based on the resolved IP and device type.
     *
     * @param hostname The mDNS hostname (without .local).
     * @param timeout_ms Timeout for the entire operation.
     * @return ESP_OK on success, or an error code on failure.
     */
    esp_err_t startOtaWithMdns(const std::string &hostname,
                               uint32_t timeout_ms = 120000);

    /**
     * @brief Check if an OTA update is currently in progress.
     * @return True if an OTA task is running, false otherwise.
     */
    bool isOtaInProgress() const;

    /**
     * @brief Set the device type for constructing the firmware URL with mDNS.
     * @param device_type A string identifier (e.g., "water_tank").
     */
    void setDeviceType(const std::string &device_type);

    /**
     * @brief Get the currently configured device type.
     * @return The device type string.
     */
    std::string getDeviceType() const;

private:
    OtaManager();
    ~OtaManager();

    // Task parameters structure (passed to task)
    struct OtaTaskParams
    {
        std::string url_or_hostname;
        bool use_mdns;
        OtaManager *manager;
        SemaphoreHandle_t completion_semaphore; // Signals task completion
        esp_err_t *result_ptr;                  // To store the final result

        OtaTaskParams(const std::string &u,
                      bool mdns,
                      OtaManager *m,
                      SemaphoreHandle_t sem,
                      esp_err_t *res)
            : url_or_hostname(u)
            , use_mdns(mdns)
            , manager(m)
            , completion_semaphore(sem)
            , result_ptr(res)
        {
        }
    };

    // Internal helpers
    esp_err_t startOtaProcess(const std::string &url_or_hostname,
                              bool use_mdns,
                              uint32_t timeout_ms);
    esp_err_t resolveServerMdns(const std::string &hostname, std::string &out_ip);
    void cleanupOtaTask(OtaTaskParams *params, esp_err_t result);
    esp_err_t performOtaDownload(const std::string &url);

    // Task function (created on demand)
    static void otaTask(void *pvParameters);

    // State management (thread-safe)
    void setOtaInProgress(bool in_progress);

    // Internal state
    std::string device_type_;
    SemaphoreHandle_t state_mutex_;
    bool ota_in_progress_;
    OtaManagerCallbacks callbacks_;
};
