#include "ota_manager.hpp"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "nvs_flash.h"
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

const char *OtaManager::TAG               = "OTA";
OtaManager *OtaManager::instance_         = nullptr;
SemaphoreHandle_t OtaManager::instance_mutex_ = xSemaphoreCreateMutex();

OtaManager::OtaManager()
    : wifi_connected_(false)
    , device_type_("default")
{
}

OtaManager::~OtaManager()
{
    if (wifi_connected_) {
        disconnectWiFi();
    }
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

// ============ Credenciais ============

esp_err_t OtaManager::storeCredentials(const std::string &ssid,
                                       const std::string &password)
{
    if (ssid.empty() || ssid.length() >= 32) {
        ESP_LOGE(TAG, "SSID inválido");
        return ESP_ERR_INVALID_ARG;
    }

    if (!password.empty() && password.length() < 8) {
        ESP_LOGE(TAG, "Senha muito curta");
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open("ota_manager", NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao abrir NVS: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(h, "ssid", ssid.c_str());
    if (err == ESP_OK) {
        err = nvs_set_str(h, "pass", password.c_str());
    }

    if (err == ESP_OK) {
        err = nvs_commit(h);
    }

    nvs_close(h);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Credenciais salvas: %s", ssid.c_str());
    }

    return err;
}

esp_err_t OtaManager::loadCredentials(std::string &ssid, std::string &password)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("ota_manager", NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao abrir NVS para leitura: %s", esp_err_to_name(err));
        return err;
    }

    char ssid_buf[32] = {0};
    char pass_buf[64] = {0};
    size_t ssid_len   = sizeof(ssid_buf);
    size_t pass_len   = sizeof(pass_buf);

    err = nvs_get_str(h, "ssid", ssid_buf, &ssid_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao ler SSID do NVS: %s", esp_err_to_name(err));
    } else {
        err = nvs_get_str(h, "pass", pass_buf, &pass_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Falha ao ler senha do NVS: %s", esp_err_to_name(err));
        }
    }

    nvs_close(h);

    if (err == ESP_OK) {
        ssid     = ssid_buf;
        password = pass_buf;
        ESP_LOGI(TAG, "Credenciais carregadas: %s", ssid.c_str());
    }

    return err;
}

bool OtaManager::hasCredentials()
{
    std::string ssid, pass;
    return loadCredentials(ssid, pass) == ESP_OK && !ssid.empty();
}

void OtaManager::clearCredentials()
{
    nvs_handle_t h;
    if (nvs_open("ota_manager", NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, "ssid");
        nvs_erase_key(h, "pass");
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "Credenciais removidas");
    }
}

// ============ Conexão WiFi ============

esp_err_t OtaManager::connectWiFi(uint32_t timeout_ms)
{
    std::string ssid, password;
    esp_err_t err = loadCredentials(ssid, password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Credenciais não encontradas");
        return err;
    }

    return connectWiFi(ssid, password, timeout_ms);
}

esp_err_t OtaManager::connectWiFi(const std::string &ssid,
                                  const std::string &password,
                                  uint32_t timeout_ms)
{
    ESP_LOGI(TAG, "Conectando WiFi: %s", ssid.c_str());

    esp_wifi_set_mode(WIFI_MODE_STA);

    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid, ssid.c_str(), sizeof(cfg.sta.ssid) - 1);
    strncpy((char *)cfg.sta.password, password.c_str(), sizeof(cfg.sta.password) - 1);

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao configurar WiFi: %s", esp_err_to_name(err));
        return err;
    }

    const int max_retries = 3;
    for (int i = 0; i < max_retries; ++i) {
        ESP_LOGI(TAG, "Tentativa de conexão %d/%d...", i + 1, max_retries);
        err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Falha ao chamar esp_wifi_connect: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        if (waitForIp(timeout_ms)) {
            wifi_connected_ = true;
            return ESP_OK;
        }

        ESP_LOGW(TAG, "WiFi timeout na tentativa %d. Tentando novamente em 5 segundos...", i + 1);
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(5000));
    }

    ESP_LOGE(TAG, "Falha ao conectar ao WiFi após %d tentativas.", max_retries);
    return ESP_ERR_TIMEOUT;
}

bool OtaManager::waitForIp(uint32_t timeout_ms)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        ESP_LOGE(TAG, "Netif não encontrado");
        return false;
    }

    for (uint32_t i = 0; i < timeout_ms / 500; i++) {
        esp_netif_ip_info_t ip_info;

        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr
!= 0) {
            ESP_LOGI(TAG, "WiFi conectado: " IPSTR, IP2STR(&ip_info.ip));
            return true;
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }

    return false;
}

void OtaManager::disconnectWiFi()
{
    if (!wifi_connected_)
        return;

    ESP_LOGI(TAG, "Desconectando WiFi");
    esp_wifi_disconnect();
    esp_wifi_stop();
    wifi_connected_ = false;
    vTaskDelay(pdMS_TO_TICKS(500));
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
    std::string full_url = url + ":" + std::to_string(port) + "/" + device_type_
 + path;

    return performOta(full_url);
}

esp_err_t OtaManager::resolveServerMdns(const std::string &hostname, std::string
 &url)
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
