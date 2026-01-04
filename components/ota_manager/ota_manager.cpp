#include "ota_manager.hpp"
#include "esp_https_ota.h"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"
#include "mdns.h"
#include <cstring>

#include "common_types.hpp"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "OTA";

// Constructor - private
OtaManager::OtaManager()
    : device_type_("default")
    , ota_in_progress_(false)
{
    state_mutex_ = xSemaphoreCreateMutex();
    if (state_mutex_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create mutex");
    }
}

// Destructor - private
OtaManager::~OtaManager()
{
    if (state_mutex_ != nullptr) {
        vSemaphoreDelete(state_mutex_);
    }
}

// Singleton instance - static
OtaManager &OtaManager::instance()
{
    static OtaManager instance;
    return instance;
}

// ============ Initialization ============

esp_err_t OtaManager::init()
{
    ESP_LOGI(TAG, "Initializing OTA Manager");

    // Register event handler for OTA commands
    esp_err_t err = esp_event_handler_instance_register(
        APP_OTA_EVENT, ESP_EVENT_ANY_ID, &OtaManager::eventHandler, this, nullptr);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register event handler: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "OTA Manager initialized successfully");
    return ESP_OK;
}

void OtaManager::deinit()
{
    esp_event_handler_unregister(APP_OTA_EVENT, ESP_EVENT_ANY_ID,
                                 &OtaManager::eventHandler);
    ESP_LOGI(TAG, "OTA Manager deinitialized");
}

// ============ Event Handler ============

void OtaManager::eventHandler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    OtaManager *manager = static_cast<OtaManager *>(arg);

    if (base != APP_OTA_EVENT) {
        return;
    }

    switch (id) {
    case OTA_CMD_START:
    {
        if (data == nullptr) {
            ESP_LOGE(TAG, "OTA_CMD_START received without data");
            return;
        }

        std::string hostname(static_cast<const char *>(data));
        ESP_LOGI(TAG, "OTA_CMD_START received for hostname: %s", hostname.c_str());

        // Start OTA with MDNS resolution
        manager->startOtaFromEvent(hostname, true);
        break;
    }

    case OTA_CMD_STOP:
    {
        ESP_LOGI(TAG, "OTA_CMD_STOP received");
        // TODO: Implement graceful cancellation if needed
        break;
    }
    case OTA_EVT_STARTED:
        ESP_LOGI(TAG, "OTA started");
        break;
    case OTA_EVT_FAILED:
        ESP_LOGE(TAG, "OTA failed");
        break;
    case OTA_EVT_FINISHED:
        ESP_LOGW(TAG, "OTA finished");
        break;

    default:
        ESP_LOGW(TAG, "Unknown OTA event: %ld", id);
        break;
    }
}

// ============ OTA Operations ============

esp_err_t OtaManager::startOtaFromEvent(const std::string &url_or_hostname, bool use_mdns)
{
    // Check if OTA is already in progress
    if (isOtaInProgress()) {
        ESP_LOGW(TAG, "OTA already in progress, ignoring request");
        return ESP_ERR_INVALID_STATE;
    }

    // Allocate task parameters on heap (will be freed by task)
    OtaTaskParams *params = new OtaTaskParams(url_or_hostname, use_mdns, this);
    if (params == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate task parameters");
        return ESP_ERR_NO_MEM;
    }

    // Create OTA task
    BaseType_t result = xTaskCreate(otaTask, "ota_task",
                                    8192, // Stack size
                                    params,
                                    5,      // Priority (above idle, below WiFi/ESPNOW)
                                    nullptr // Don't store handle
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create OTA task");
        delete params;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA task created successfully");
    return ESP_OK;
}

void OtaManager::otaTask(void *pvParameters)
{
    OtaTaskParams *params = static_cast<OtaTaskParams *>(pvParameters);
    OtaManager *manager   = params->manager;

    // Set OTA in progress
    manager->setOtaInProgress(true);

    // Post OTA started event
    esp_event_post(APP_OTA_EVENT, OTA_EVT_STARTED, nullptr, 0, 0);

    std::string url;
    esp_err_t err = ESP_OK;

    // Resolve URL (MDNS or direct)
    if (params->use_mdns) {
        err = manager->resolveServerMdns(params->url, url);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "MDNS resolution failed");
            esp_event_post(APP_OTA_EVENT, OTA_EVT_FAILED, &err, sizeof(err), 0);
            manager->cleanupOtaTask(params);
            return;
        }

        // Build full URL: http://IP:port/device_type/device_type.bin
        url = "http://" + url + ":8070/" + manager->getDeviceType() + "/" +
              manager->getDeviceType() + ".bin";
    }
    else {
        url = params->url;
    }

    ESP_LOGI(TAG, "Starting OTA from: %s", url.c_str());

    // Perform OTA
    err = manager->performOtaDownload(url);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA successful! Restarting...");

        // Post OTA finished event (though restart will happen immediately)
        esp_event_post(APP_OTA_EVENT, OTA_EVT_FINISHED, nullptr, 0, 0);

        // Give time for event to be processed
        vTaskDelay(pdMS_TO_TICKS(100));

        esp_restart();
        // Code never reaches here
    }
    else {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
        esp_event_post(APP_OTA_EVENT, OTA_EVT_FAILED, &err, sizeof(err), 0);
        manager->cleanupOtaTask(params);
        return;
    }
}

// ============ MDNS Resolution ============

esp_err_t OtaManager::resolveServerMdns(const std::string &hostname, std::string &url)
{
    ESP_LOGI(TAG, "Resolving %s.local via mDNS...", hostname.c_str());

    // Try to initialize (ignore if already initialized)
    esp_err_t init_err = mdns_init();
    if (init_err != ESP_OK && init_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize mDNS: %s", esp_err_to_name(init_err));
        return init_err;
    }

    esp_ip4_addr_t addr;
    addr.addr = 0;

    esp_err_t err = mdns_query_a(hostname.c_str(), 5000, &addr);

    ESP_LOGI(TAG, "Query result: %s, addr: " IPSTR, esp_err_to_name(err), IP2STR(&addr));

    if (err == ESP_OK && addr.addr != 0) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&addr));
        url = ip_str;

        ESP_LOGI(TAG, "Server found: %s", url.c_str());
        mdns_free();
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to resolve %s.local", hostname.c_str());
    mdns_free();
    return ESP_FAIL;
}

// ============ Configuration ============

void OtaManager::setDeviceType(const std::string &device_type)
{
    device_type_ = device_type;
    ESP_LOGI(TAG, "Device type set to: %s", device_type_.c_str());
}

std::string OtaManager::getDeviceType() const
{
    return device_type_;
}

// ============ Status Queries ============

bool OtaManager::isOtaInProgress() const
{
    if (state_mutex_ == nullptr) {
        return ota_in_progress_;
    }

    xSemaphoreTake(state_mutex_, portMAX_DELAY);
    bool in_progress = ota_in_progress_;
    xSemaphoreGive(state_mutex_);
    return in_progress;
}

// ============ Internal State Management ============

void OtaManager::setOtaInProgress(bool in_progress)
{
    if (state_mutex_ == nullptr) {
        ota_in_progress_ = in_progress;
        return;
    }

    xSemaphoreTake(state_mutex_, portMAX_DELAY);
    ota_in_progress_ = in_progress;
    xSemaphoreGive(state_mutex_);

    ESP_LOGD(TAG, "OTA in progress: %s", in_progress ? "true" : "false");
}

// ============ Public Wrappers (for direct calls) ============

esp_err_t OtaManager::startOta(const std::string &url)
{
    return startOtaFromEvent(url, false);
}

esp_err_t OtaManager::startOtaWithMdns(const std::string &hostname)
{
    return startOtaFromEvent(hostname, true);
}

void OtaManager::cleanupOtaTask(OtaTaskParams *params)
{
    setOtaInProgress(false);
    if (params != nullptr) {
        delete params;
    }

    vTaskDelete(nullptr); // Delete current task
}

esp_err_t OtaManager::performOtaDownload(const std::string &url)
{
    esp_http_client_config_t http_config = {.url            = url.c_str(),
                                            .cert_pem       = NULL,
                                            .timeout_ms     = 10000,
                                            .transport_type = HTTP_TRANSPORT_OVER_TCP,
                                            .skip_cert_common_name_check = true,
                                            .keep_alive_enable           = true};

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    return esp_https_ota(&ota_config);
}