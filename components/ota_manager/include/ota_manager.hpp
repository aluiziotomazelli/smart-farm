#pragma once

#include "esp_err.h"
#include "esp_event.h"
#include <cstdint>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class OtaManager
{
public:
    // Singleton with reference (safer, cleaner syntax)
    static OtaManager &instance();

    // Prevent copying
    OtaManager(const OtaManager &)            = delete;
    OtaManager &operator=(const OtaManager &) = delete;

    // Initialize OTA manager and register event handler
    esp_err_t init();

    // OTA operations
    esp_err_t startOta(const std::string &url);
    esp_err_t startOtaWithMdns(const std::string &hostname);

    // Status queries (thread-safe)
    bool isOtaInProgress() const;

    // Configuration
    void setDeviceType(const std::string &device_type);
    std::string getDeviceType() const;

    // Cleanup (optional, for completeness)
    void deinit();

private:
    OtaManager();
    ~OtaManager();

    // Task parameters structure (passed to task, dynamically allocated)
    struct OtaTaskParams
    {
        std::string url;
        bool use_mdns;
        OtaManager *manager;

        OtaTaskParams(const std::string &u, bool mdns, OtaManager *m)
            : url(u)
            , use_mdns(mdns)
            , manager(m)
        {
        }
    };

    // Internal helpers
    esp_err_t startOtaFromEvent(const std::string &url_or_hostname, bool use_mdns);
    esp_err_t resolveServerMdns(const std::string &hostname, std::string &url);
    void cleanupOtaTask(OtaTaskParams *params);
    esp_err_t performOtaDownload(const std::string &url);

    // Task function (created on demand)
    static void otaTask(void *pvParameters);

    // Event handler (static)
    static void eventHandler(void *arg, esp_event_base_t base, int32_t id, void *data);

    // State management (thread-safe)
    void setOtaInProgress(bool in_progress);

    // Internal state
    std::string device_type_;
    SemaphoreHandle_t state_mutex_;
    bool ota_in_progress_;
};
