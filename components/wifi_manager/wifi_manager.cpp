#include "wifi_manager.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include <cstring>

static const char *TAG = "WiFiManager";

WiFiManager::WiFiManager()
    : initialized_(false)
    , started_(false)
    , connected_(false)
{
    state_mutex_ = xSemaphoreCreateMutex();
    if (state_mutex_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create state mutex");
    }
    ip_got_sem_ = xSemaphoreCreateBinary();
    if (ip_got_sem_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create IP semaphore");
    }
    else {
        xSemaphoreTake(ip_got_sem_, 0);
    }
}

WiFiManager::~WiFiManager()
{
    if (state_mutex_ != nullptr) {
        vSemaphoreDelete(state_mutex_);
    }
    if (ip_got_sem_ != nullptr) {
        vSemaphoreDelete(ip_got_sem_);
    }
}

WiFiManager &WiFiManager::instance()
{
    static WiFiManager instance;
    return instance;
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

    err = esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &WiFiManager::wifiEventHandler, this, nullptr);
    if (err == ESP_OK) {
        err = esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, &WiFiManager::ipEventHandler, this, nullptr);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register event handlers");
        return err;
    }

    initialized_ = true;
    return ESP_OK;
}

void WiFiManager::wifiEventHandler(void *arg,
                                   esp_event_base_t base,
                                   int32_t id,
                                   void *data)
{
    WiFiManager *self = static_cast<WiFiManager *>(arg);

    if (self->state_mutex_ == nullptr) {
        return; // Mutex não criado ainda
    }

    if (xSemaphoreTake(self->state_mutex_, portMAX_DELAY) != pdTRUE) {
        return; // Não conseguiu pegar mutex
    }

    switch (id) {
    case WIFI_EVENT_STA_START:
        self->started_ = true;
        ESP_LOGI(TAG, "WiFi started (event)");
        break;

    case WIFI_EVENT_STA_CONNECTED:
        self->connected_ = true;
        ESP_LOGI(TAG, "WiFi connected (event)");
        break;

    case WIFI_EVENT_STA_DISCONNECTED:
        self->connected_ = false;
        ESP_LOGI(TAG, "WiFi disconnected (event)");
        // Limpar semáforo de IP (se estava esperando)
        xSemaphoreTake(self->ip_got_sem_, 0);
        break;

    case WIFI_EVENT_STA_STOP:
        self->started_   = false;
        self->connected_ = false;
        ESP_LOGI(TAG, "WiFi stopped (event)");
        xSemaphoreTake(self->ip_got_sem_, 0); // Limpar semáforo
        break;

    default:
        break;
    }

    xSemaphoreGive(self->state_mutex_);
}

void WiFiManager::ipEventHandler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    WiFiManager *self = static_cast<WiFiManager *>(arg);

    if (id == IP_EVENT_STA_GOT_IP && self->ip_got_sem_ != nullptr) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        char ip_str[16];

        // Format and log IP address
        esp_ip4addr_ntoa(&event->ip_info.ip, ip_str, sizeof(ip_str));
        ESP_LOGI(TAG, "Got IP address: %s", ip_str);

        // Format and log gateway address (reusing buffer)
        esp_ip4addr_ntoa(&event->ip_info.gw, ip_str, sizeof(ip_str));
        ESP_LOGI(TAG, "Gateway: %s", ip_str);

        xSemaphoreGive(self->ip_got_sem_); // Sinaliza que tem IP
    }
}

esp_err_t WiFiManager::start()
{
    if (!initialized_) {
        ESP_LOGE(TAG, "Must call init() before start().");
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(state_mutex_, portMAX_DELAY);
    bool already_started = started_;
    xSemaphoreGive(state_mutex_);
    if (already_started) {
        ESP_LOGI(TAG, "Already started.");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting Wi-Fi...");
    esp_err_t err = (esp_wifi_set_mode(WIFI_MODE_STA));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set mode: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start: %s", esp_err_to_name(err));
        return err;
    }

    TickType_t start = xTaskGetTickCount();
    while (xTaskGetTickCount() - start < pdMS_TO_TICKS(5000)) {
        xSemaphoreTake(state_mutex_, portMAX_DELAY);
        bool started_received = started_;
        xSemaphoreGive(state_mutex_);
        if (started_received) {
            ESP_LOGI(TAG, "WiFi confirmed started");
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGW(TAG, "WiFi start command sent, but no START event received");
    return ESP_OK;
}

esp_err_t WiFiManager::stop()
{
    ESP_LOGI(TAG, "Stopping Wi-Fi...");

    xSemaphoreTake(state_mutex_, portMAX_DELAY);
    bool already_stopped = !started_;
    xSemaphoreGive(state_mutex_);
    if (already_stopped) {
        ESP_LOGI(TAG, "Already stopped.");
        return ESP_OK;
    }

    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop: %s", esp_err_to_name(err));
        return err;
    }

    TickType_t start = xTaskGetTickCount();
    while (xTaskGetTickCount() - start < pdMS_TO_TICKS(5000)) {
        xSemaphoreTake(state_mutex_, portMAX_DELAY);
        bool stopped_received = !started_;
        xSemaphoreGive(state_mutex_);
        if (stopped_received) {
            ESP_LOGI(TAG, "WiFi confirmed stopped");
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGW(TAG, "WiFi stop command sent, but no STOP event received");
    return ESP_OK;
}

esp_err_t WiFiManager::connect(const std::string &ssid,
                               const std::string &password,
                               uint32_t timeout_ms)
{
    ESP_LOGI(TAG, "Connecting Wi-Fi...");

    xSemaphoreTake(state_mutex_, portMAX_DELAY);
    bool not_started       = !started_;
    bool already_connected = connected_;
    xSemaphoreGive(state_mutex_);
    if (not_started) {
        ESP_LOGI(TAG, "Must call start() before connect().");
        return ESP_OK;
    }
    if (already_connected) {
        ESP_LOGI(TAG, "Already connected, disconnecting first...");
        esp_err_t err = disconnect();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to disconnect: %s", esp_err_to_name(err));
            return err;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid.c_str());

    wifi_config_t wifi_config = {};
    strncpy((char *)wifi_config.sta.ssid, ssid.c_str(), sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password.c_str(),
            sizeof(wifi_config.sta.password) - 1);

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config() failed: %s", esp_err_to_name(err));
        return err;
    }
    if (ip_got_sem_ != nullptr) {
        xSemaphoreTake(ip_got_sem_, 0);
    }
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect() failed: %s", esp_err_to_name(err));
        return err;
    }

    if (waitForIp(timeout_ms)) {
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to connect after %lu ms.", timeout_ms);
    return ESP_ERR_TIMEOUT;
}

esp_err_t WiFiManager::disconnect()
{
    ESP_LOGI(TAG, "Disconnecting Wi-Fi...");
    xSemaphoreTake(state_mutex_, portMAX_DELAY);
    bool already_disconnected = !connected_;
    xSemaphoreGive(state_mutex_);
    if (already_disconnected) {
        ESP_LOGI(TAG, "Already disconnected.");
        return ESP_OK;
    }

    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop: %s", esp_err_to_name(err));
        return err;
    }

    TickType_t start = xTaskGetTickCount();
    while (xTaskGetTickCount() - start < pdMS_TO_TICKS(5000)) {
        xSemaphoreTake(state_mutex_, portMAX_DELAY);
        bool disconnected_received = !connected_;
        xSemaphoreGive(state_mutex_);
        if (disconnected_received) {
            ESP_LOGI(TAG, "WiFi confirmed stopped");
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGW(TAG, "WiFi stop command sent, but no STOP event received");
    return ESP_OK;
}

bool WiFiManager::waitForIp(uint32_t timeout_ms)
{
    if (ip_got_sem_ == nullptr) {
        return false;
    }

    // Aguardar semáforo (IP_EVENT_STA_GOT_IP dará xSemaphoreGive)
    bool got_ip = (xSemaphoreTake(ip_got_sem_, pdMS_TO_TICKS(timeout_ms)) == pdTRUE);

    // Se não conseguiu IP em timeout_ms, limpa semáforo
    if (got_ip) {
        ESP_LOGI(TAG, "Got IP confirmed");
    }
    else {
        ESP_LOGW(TAG, "IP timeout after %lu ms", timeout_ms);
        xSemaphoreTake(ip_got_sem_, 0);
    }

    return got_ip;
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

bool WiFiManager::isConnected() const
{
    xSemaphoreTake(state_mutex_, portMAX_DELAY);
    bool connected = connected_;
    xSemaphoreGive(state_mutex_);
    return connected;
}

bool WiFiManager::isStarted() const
{
    xSemaphoreTake(state_mutex_, portMAX_DELAY);
    bool started = started_;
    xSemaphoreGive(state_mutex_);
    return started;
}