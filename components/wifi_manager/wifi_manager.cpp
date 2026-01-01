#include "wifi_manager.hpp"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include <cstring>

const char *WiFiManager::TAG                   = "WiFiManager";
WiFiManager *WiFiManager::instance_            = nullptr;
SemaphoreHandle_t WiFiManager::instance_mutex_ = xSemaphoreCreateMutex();

WiFiManager::WiFiManager()
    : initialized_(false)
    , started_(false)
    , connected_(false)
{
}

WiFiManager::~WiFiManager()
{
    // Cleanup resources if necessary
}

WiFiManager *WiFiManager::getInstance()
{
    xSemaphoreTake(instance_mutex_, portMAX_DELAY);
    if (instance_ == nullptr) {
        instance_ = new WiFiManager();
    }
    xSemaphoreGive(instance_mutex_);
    return instance_;
}

esp_err_t WiFiManager::init()
{
    if (initialized_) {
        ESP_LOGI(TAG, "Already initialized.");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Initializing network stack...");
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK)
        return err;

    err = esp_event_loop_create_default();
    if (err != ESP_OK)
        return err;

    if (esp_netif_create_default_wifi_sta() == nullptr) {
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err                    = esp_wifi_init(&cfg);
    if (err != ESP_OK)
        return err;
    initialized_ = true;
    return ESP_OK;
}

esp_err_t WiFiManager::start()
{
    if (!initialized_) {
        ESP_LOGE(TAG, "Must call init() before start().");
        return ESP_ERR_INVALID_STATE;
    }
    if (started_) {
        ESP_LOGI(TAG, "Already started.");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Starting Wi-Fi...");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    esp_err_t err = esp_wifi_start();
    if (err == ESP_OK) {
        started_ = true;
    }
    return err;
}

esp_err_t WiFiManager::stop()
{
    if (!started_) {
        ESP_LOGI(TAG, "Already stopped.");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Stopping Wi-Fi...");
    esp_err_t err = esp_wifi_stop();
    if (err == ESP_OK) {
        started_ = false;
    }
    return err;
}

esp_err_t WiFiManager::connect(const std::string &ssid,
                               const std::string &password,
                               uint32_t timeout_ms)
{
    if (!started_) {
        ESP_LOGE(TAG, "Must call start() before connect().");
        return ESP_ERR_INVALID_STATE;
    }
    if (connected_) {
        disconnect();
    }

    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid.c_str());

    wifi_config_t wifi_config = {};
    strncpy((char *)wifi_config.sta.ssid, ssid.c_str(), sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password.c_str(),
            sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect() failed: %s", esp_err_to_name(err));
        return err;
    }

    if (waitForIp(timeout_ms)) {
        connected_ = true;
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to connect after %lu ms.", timeout_ms);
    return ESP_ERR_TIMEOUT;
}

esp_err_t WiFiManager::disconnect()
{
    if (!connected_) {
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Disconnecting from Wi-Fi...");
    esp_err_t err = esp_wifi_disconnect();
    if (err == ESP_OK) {
        connected_ = false;
    }
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        ESP_LOGW(TAG, "WiFi ainda conectado! Forçando desconexão...");
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return err;
}

bool WiFiManager::waitForIp(uint32_t timeout_ms)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        return false;
    }

    for (uint32_t i = 0; i < timeout_ms / 100; i++) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
            ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&ip_info.ip));
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return false;
}

esp_err_t WiFiManager::storeCredentials(const std::string &ssid,
                                        const std::string &password)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("wifi_manager", NVS_READWRITE, &h);
    if (err != ESP_OK)
        return err;

    err = nvs_set_str(h, "ssid", ssid.c_str());
    if (err == ESP_OK) {
        err = nvs_set_str(h, "pass", password.c_str());
    }

    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t WiFiManager::loadCredentials(std::string &ssid, std::string &password)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("wifi_manager", NVS_READONLY, &h);
    if (err != ESP_OK)
        return err;

    char ssid_buf[32] = {0};
    char pass_buf[64] = {0};
    size_t ssid_len   = sizeof(ssid_buf);
    size_t pass_len   = sizeof(pass_buf);

    err = nvs_get_str(h, "ssid", ssid_buf, &ssid_len);
    if (err == ESP_OK) {
        err = nvs_get_str(h, "pass", pass_buf, &pass_len);
    }
    nvs_close(h);

    if (err == ESP_OK) {
        ssid     = ssid_buf;
        password = pass_buf;
    }
    return err;
}

bool WiFiManager::hasCredentials()
{
    std::string ssid, pass;
    return loadCredentials(ssid, pass) == ESP_OK && !ssid.empty();
}
