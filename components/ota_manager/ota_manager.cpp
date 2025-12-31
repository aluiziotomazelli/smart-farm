#include "ota_manager.hpp"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "mdns.h"
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

const char *OtaManager::TAG                   = "OTA";
OtaManager *OtaManager::instance_             = nullptr;
SemaphoreHandle_t OtaManager::instance_mutex_ = xSemaphoreCreateMutex();

OtaManager::OtaManager()
    : device_type_("default")
{
}

OtaManager::~OtaManager()
{
}

OtaManager *OtaManager::getInstance()
{
    xSemaphoreTake(instance_mutex_, portMAX_DELAY);
    if (instance_ == nullptr) {
        instance_ = new OtaManager();
    }
    xSemaphoreGive(instance_mutex_);
    return instance_;
}

// ============ OTA ============

esp_err_t OtaManager::performOta(const std::string &url)
{
    ESP_LOGI(TAG, "Iniciando OTA de: %s", url.c_str());

    esp_http_client_config_t http_config = {
        .url               = url.c_str(),
        .timeout_ms        = 10000,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_err_t ret = esp_https_ota(&ota_config);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✓ OTA bem-sucedido! Reiniciando...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    }
    else {
        ESP_LOGE(TAG, "✗ OTA falhou: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t OtaManager::performOtaWithMdns(const std::string &hostname,
                                         uint16_t port,
                                         const std::string &path)
{
    std::string url;
    esp_err_t err = resolveServerMdns(hostname, url);
    if (err != ESP_OK) {
        return err;
    }

    // Construir URL completa
    std::string full_url = url + ":" + std::to_string(port) + "/" + device_type_ + path;

    return performOta(full_url);
}

esp_err_t OtaManager::resolveServerMdns(const std::string &hostname, std::string &url)
{
    ESP_LOGI(TAG, "Resolvendo %s.local via mDNS...", hostname.c_str());

    mdns_init();

    esp_ip4_addr_t addr;
    addr.addr = 0;

    esp_err_t err = mdns_query_a(hostname.c_str(), 5000, &addr);

    if (err == ESP_OK && addr.addr != 0) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&addr));
        url = std::string("http://") + ip_str;

        ESP_LOGI(TAG, "✓ Servidor encontrado: %s", url.c_str());
        mdns_free();
        return ESP_OK;
    }

    ESP_LOGE(TAG, "✗ Falha ao resolver %s.local", hostname.c_str());
    mdns_free();
    return ESP_FAIL;
}

// ============ Configuração ============

void OtaManager::setDeviceType(const std::string &device_type)
{
    device_type_ = device_type;
    ESP_LOGI(TAG, "Device type: %s", device_type_.c_str());
}

std::string OtaManager::getDeviceType() const
{
    return device_type_;
}
