#include "central_hub_app.hpp"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "CentralHubApp";

CentralHubApp::CentralHubApp()
{
}

void CentralHubApp::on_espnow_receive(uint8_t node_id,
                                      const uint8_t *data,
                                      int len,
                                      int8_t rssi)
{
    ESP_LOGI(TAG, "Received %d bytes from node %u (RSSI: %d dBm)", len, node_id, rssi);

    if (len == sizeof(uint16_t)) {
        uint16_t received_level;
        memcpy(&received_level, data, sizeof(received_level));
        ESP_LOGI(TAG, "Received water level: %u‰ from node %u", received_level, node_id);
    }
}

void CentralHubApp::init()
{
    ESP_LOGI(TAG, "Initializing CentralHubApp");
    storage_.init_partition();

    if (storage_.load() != ESP_OK) {
        ESP_LOGW(TAG, "NVS load failed, performing factory reset");
        storage_.factory_reset();
    }

    auto &core = storage_.getCoreData();
    if (core.node_id == 0) {
        ESP_LOGI(TAG, "Node ID not set, generating from MAC address...");
        uint8_t mac[6];
        esp_efuse_mac_get_default(mac);
        core.node_id = mac[3] ^ mac[4] ^ mac[5];
        if (storage_.commit() == ESP_OK) {
            ESP_LOGI(TAG, "New Node ID %u saved to NVS.", core.node_id);
        } else {
            ESP_LOGE(TAG, "Failed to save new Node ID to NVS!");
        }
    }

    // Initialize ESP-NOW communication
    ESPNOWConfig config;
    config.wifi_channel = 0; // Auto channel
    config.max_peers = 10;
    config.auto_pairing = true;
    config.allow_broadcast = true;

    if (!comm_.init(config)) {
        ESP_LOGE(TAG, "Failed to initialize ESP-NOW");
        return;
    }

    ESP_LOGI(TAG, "ESP-NOW initialized. Our node ID: %u", comm_.get_id());

    comm_.setReceiveCallback(
        [this](uint8_t node_id, const uint8_t *data, int len, int8_t rssi) {
            this->on_espnow_receive(node_id, data, len, rssi);
        });
}

void CentralHubApp::run()
{
    ESP_LOGI(TAG, "Starting Application Loop");
    while (true) {
        comm_.process();
        vTaskDelay(pdMS_TO_TICKS(100)); // Small delay to prevent busy-waiting
    }
}
