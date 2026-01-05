#include "ota_manager.hpp"

#include "esp_https_ota.h"
#include "esp_log.h"
#include "mdns.h"
#include <cstring>
#include <string>

static const char *TAG = "OTA";

// =================================================================================================
// Singleton and Constructor/Destructor
// =================================================================================================

OtaManager &OtaManager::instance()
{
    static OtaManager instance;
    return instance;
}

OtaManager::OtaManager()
    : device_type_("default")
    , ota_in_progress_(false)
{
    state_mutex_ = xSemaphoreCreateMutex();
    if (state_mutex_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create state mutex");
    }
}

OtaManager::~OtaManager()
{
    if (state_mutex_ != nullptr) {
        vSemaphoreDelete(state_mutex_);
    }
}

// =================================================================================================
// Public API
// =================================================================================================

esp_err_t OtaManager::init()
{
    ESP_LOGI(TAG, "OTA Manager initialized");
    return ESP_OK;
}

void OtaManager::registerCallbacks(const OtaManagerCallbacks &callbacks)
{
    callbacks_ = callbacks;
}

esp_err_t OtaManager::startOta(const std::string &url, uint32_t timeout_ms)
{
    // Port is not used for direct URL OTA, so pass 0.
    return startOtaProcess(url, false, timeout_ms, 0);
}

esp_err_t OtaManager::startOtaWithMdns(const std::string &hostname,
                                       uint32_t timeout_ms,
                                       uint16_t port)
{
    return startOtaProcess(hostname, true, timeout_ms, port);
}

// =================================================================================================
// Internal Implementation
// =================================================================================================

esp_err_t OtaManager::startOtaProcess(const std::string &url_or_hostname,
                                      bool use_mdns,
                                      uint32_t timeout_ms,
                                      uint16_t port)
{
    // Thread-safe check to prevent concurrent OTA operations
    xSemaphoreTake(state_mutex_, portMAX_DELAY);
    if (ota_in_progress_) {
        xSemaphoreGive(state_mutex_);
        ESP_LOGW(TAG, "OTA already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    // Mark as in progress inside the lock
    ota_in_progress_ = true;
    xSemaphoreGive(state_mutex_);

    esp_err_t result                     = ESP_FAIL;
    SemaphoreHandle_t completion_semaphore = xSemaphoreCreateBinary();
    if (completion_semaphore == nullptr) {
        ESP_LOGE(TAG, "Failed to create completion semaphore");
        setOtaInProgress(false); // Reset state
        return ESP_ERR_NO_MEM;
    }

    OtaTaskParams *params = new OtaTaskParams(
        url_or_hostname, use_mdns, port, this, completion_semaphore, &result);
    if (params == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate task parameters");
        vSemaphoreDelete(completion_semaphore);
        setOtaInProgress(false); // Reset state
        return ESP_ERR_NO_MEM;
    }

    BaseType_t task_created = xTaskCreate(otaTask, "ota_task", 8192, params, 5, nullptr);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create OTA task");
        delete params;
        vSemaphoreDelete(completion_semaphore);
        setOtaInProgress(false); // Reset state
        return ESP_FAIL;
    }

    // Block and wait for the OTA task to complete or timeout
    ESP_LOGI(TAG, "Waiting for OTA to complete...");
    if (xSemaphoreTake(completion_semaphore, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        ESP_LOGI(TAG, "OTA process finished with result: %s", esp_err_to_name(result));
    }
    else {
        ESP_LOGE(TAG, "OTA process timed out after %lums", timeout_ms);
        result = ESP_ERR_TIMEOUT;
    }

    vSemaphoreDelete(completion_semaphore);
    // The task itself is responsible for calling setOtaInProgress(false)
    return result;
}

void OtaManager::otaTask(void *pvParameters)
{
    OtaTaskParams *params = static_cast<OtaTaskParams *>(pvParameters);
    OtaManager *manager   = params->manager;

    // No need to set ota_in_progress here, it's done in startOtaProcess

    if (manager->callbacks_.onOtaStarted) {
        manager->callbacks_.onOtaStarted();
    }

    std::string url;
    esp_err_t err = ESP_OK;

    if (params->use_mdns) {
        std::string ip;
        err = manager->resolveServerMdns(params->url_or_hostname, ip);
        if (err == ESP_OK) {
            // Build URL with dynamic port
            url = "http://" + ip + ":" + std::to_string(params->port) + "/" +
                  manager->getDeviceType() + "/" + manager->getDeviceType() + ".bin";
        }
    }
    else {
        url = params->url_or_hostname;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to determine OTA URL");
        manager->cleanupOtaTask(params, err);
        return;
    }

    ESP_LOGI(TAG, "Starting OTA from URL: %s", url.c_str());
    err = manager->performOtaDownload(url);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA successful! Restarting...");
        if (manager->callbacks_.onOtaFinished) {
            manager->callbacks_.onOtaFinished();
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // Allow callbacks/logs to process
        esp_restart();
    }
    else {
        ESP_LOGE(TAG, "OTA download failed: %s", esp_err_to_name(err));
        manager->cleanupOtaTask(params, err);
    }
}

void OtaManager::cleanupOtaTask(OtaTaskParams *params, esp_err_t result)
{
    if (params->result_ptr != nullptr) {
        *params->result_ptr = result;
    }

    if (result != ESP_OK && callbacks_.onOtaFailed) {
        callbacks_.onOtaFailed(result);
    }

    if (params->completion_semaphore != nullptr) {
        xSemaphoreGive(params->completion_semaphore);
    }

    setOtaInProgress(false);

    if (params != nullptr) {
        delete params;
    }
    vTaskDelete(nullptr);
}

esp_err_t OtaManager::resolveServerMdns(const std::string &hostname, std::string &out_ip)
{
    ESP_LOGI(TAG, "Resolving %s.local via mDNS...", hostname.c_str());
    mdns_init();

    esp_ip4_addr_t addr = {};
    esp_err_t err       = mdns_query_a(hostname.c_str(), 5000, &addr);

    if (err == ESP_OK) {
        if (addr.addr != 0) {
            char ip_str[16];
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&addr));
            out_ip = ip_str;
            ESP_LOGI(TAG, "Server '%s.local' found at %s", hostname.c_str(), out_ip.c_str());
            mdns_free();
            return ESP_OK;
        }
        err = ESP_ERR_NOT_FOUND;
    }

    ESP_LOGE(TAG, "Failed to resolve mDNS hostname '%s.local': %s", hostname.c_str(),
             esp_err_to_name(err));
    mdns_free();
    return err;
}

esp_err_t OtaManager::performOtaDownload(const std::string &url)
{
    esp_http_client_config_t http_config = {
        .url                         = url.c_str(),
        .timeout_ms                  = 10000,
        .transport_type              = HTTP_TRANSPORT_OVER_TCP,
        .skip_cert_common_name_check = true,
        .keep_alive_enable           = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    return esp_https_ota(&ota_config);
}

// =================================================================================================
// State Management
// =================================================================================================

void OtaManager::setDeviceType(const std::string &device_type)
{
    device_type_ = device_type;
    ESP_LOGI(TAG, "Device type set to: %s", device_type_.c_str());
}

std::string OtaManager::getDeviceType() const
{
    return device_type_;
}

void OtaManager::setOtaInProgress(bool in_progress)
{
    xSemaphoreTake(state_mutex_, portMAX_DELAY);
    ota_in_progress_ = in_progress;
    xSemaphoreGive(state_mutex_);
    ESP_LOGD(TAG, "OTA in progress state: %s", in_progress ? "true" : "false");
}