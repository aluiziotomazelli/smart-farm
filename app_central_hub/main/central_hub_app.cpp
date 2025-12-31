#include "central_hub_app.hpp"
#include "common_types.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "CentralHubApp";

CentralHubApp::CentralHubApp()
    : ota_manager_(nullptr)
{
}

void CentralHubApp::on_espnow_receive(uint8_t node_id,
                                      const uint8_t *data,
                                      int len,
                                      int8_t rssi)
{
    ESP_LOGI(TAG, "Received %d bytes from node %u (RSSI: %d dBm)", len, node_id, rssi);

    if (len == sizeof(WaterLevelReport)) {
        WaterLevelReport report;
        memcpy(&report, data, sizeof(report));
        ESP_LOGI(TAG,
                 "From node %u: Level=%u‰, Dist=%.2fcm, V=%.2fV, Q=%d, F=%d, Full=%d, "
                 "Backup=%d",
                 node_id, report.level_permille, report.distance_cm,
                 report.battery_mv / 1000.0f, (int)report.quality, (int)report.failure,
                 (int)report.float_switch_is_full, (int)report.backup_mode_active);
    } else {
        ESP_LOGW(TAG, "Received packet with unexpected size: %d bytes", len);
    }
}

void CentralHubApp::onOtaCommand(uint8_t node_id, const OtaCommand &command)
{
    if (!ota_manager_) {
        ESP_LOGE(TAG, "OtaManager not initialized!");
        return;
    }
    ESP_LOGI(TAG, "Received OTA command from node %u", node_id);
    ESP_LOGI(TAG, "URL: %s", command.url);

    // Only store credentials if a new SSID is provided
    if (strlen(command.ssid) > 0) {
        ESP_LOGI(TAG, "New credentials received. Storing SSID: %s", command.ssid);
        esp_err_t err = ota_manager_->storeCredentials(command.ssid, command.password);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to store credentials: %s", esp_err_to_name(err));
            return;
        }
    } else {
        ESP_LOGI(TAG, "No new credentials provided. Using stored credentials.");
    }

    ESP_LOGI(TAG, "Pausing ESP-NOW and starting OTA...");
    comm_.pauseForOta();

    // Connect to WiFi before starting OTA
    esp_err_t err = ota_manager_->connectWiFi();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to WiFi: %s", esp_err_to_name(err));
        comm_.resumeAfterOta();
        return;
    }

    // Perform OTA
    err = ota_manager_->performOta(command.url);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
        comm_.resumeAfterOta();
    }
    // On success, the device will restart.
}

void CentralHubApp::init()
{
    ESP_LOGI(TAG, "Initializing CentralHubApp");
    // storage_.init_partition();

    // if (storage_.load() != ESP_OK) {
    //     ESP_LOGW(TAG, "NVS load failed, performing factory reset");
    //     storage_.factory_reset();
    // }

    // auto &core = storage_.getCoreData();
    // if (core.node_id == 0) {
    //     ESP_LOGI(TAG, "Node ID not set, generating from MAC address...");
    //     uint8_t mac[6];
    //     esp_efuse_mac_get_default(mac);
    //     core.node_id = mac[3] ^ mac[4] ^ mac[5];
    //     if (storage_.commit() == ESP_OK) {
    //         ESP_LOGI(TAG, "New Node ID %u saved to NVS.", core.node_id);
    //     } else {
    //         ESP_LOGE(TAG, "Failed to save new Node ID to NVS!");
    //     }
    // }

    // Initialize ESP-NOW communication
    ESPNOWConfig config;
    config.wifi_channel = 0; // Auto channel
    config.max_peers    = 10;

    if (!comm_.init(config)) {
        ESP_LOGE(TAG, "Failed to initialize ESP-NOW");
        return;
    }

    comm_.startDiscovery(10000);

    ESP_LOGI(TAG, "ESP-NOW initialized. Our node ID: %u", comm_.get_id());

    comm_.setReceiveCallback(
        [this](uint8_t node_id, const uint8_t *data, int len, int8_t rssi) {
            this->on_espnow_receive(node_id, data, len, rssi);
        });

    comm_.setOtaCommandCallback(
        [this](uint8_t node_id, const OtaCommand &command) {
            this->onOtaCommand(node_id, command);
        });

    ota_manager_ = OtaManager::getInstance();
}

void CentralHubApp::run()
{
    ESP_LOGI(TAG, "Starting Application Loop");
    while (true) {
        comm_.process();
        vTaskDelay(pdMS_TO_TICKS(100)); // Small delay to prevent busy-waiting
    }
}
