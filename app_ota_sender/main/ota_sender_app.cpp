#include "ota_sender_app.hpp"
#include "message_types.hpp"
#include "sdkconfig.h"
#include <cstring>

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "OtaSenderApp";

OtaSenderApp::OtaSenderApp()
    : wifi_manager_(nullptr)
    , command_sent_(false)
{
}

void OtaSenderApp::init()
{
    ESP_LOGI(TAG, "Initializing OtaSenderApp");

    wifi_manager_ = WiFiManager::getInstance();
    if (wifi_manager_->init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFiManager");
        return;
    }
    if (wifi_manager_->start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFiManager");
        return;
    }

    ESPNOWConfig config;
    config.wifi_channel = 0; // Auto channel
    config.max_peers    = 1;

    if (!comm_.init(config)) {
        ESP_LOGE(TAG, "Failed to initialize ESP-NOW");
        return;
    }

    comm_.setPeerEventCallback(
        [this](const PeerInfo &peer, bool added) { this->onPeerEvent(peer, added); });

    comm_.startDiscovery(60000); // Discover indefinitely
    ESP_LOGI(TAG, "ESP-NOW initialized and discovery started. Our node ID: %u",
             comm_.get_id());
}

void OtaSenderApp::onPeerEvent(const PeerInfo &peer, bool added)
{
    if (added && !command_sent_) {
        ESP_LOGI(TAG, "Peer found: %u. Sending OTA command.", peer.node_id);
        sendOtaCommand(peer.node_id);
        command_sent_ = true;
        // comm_.stopDiscovery();
    }
}

void OtaSenderApp::sendOtaCommand(uint8_t node_id)
{
    OtaCommand command;
    snprintf(command.url, sizeof(command.url), "%s", CONFIG_OTA_SENDER_FIRMWARE_URL);
    snprintf(command.ssid, sizeof(command.ssid), "%s", CONFIG_OTA_SENDER_WIFI_SSID);
    snprintf(command.password, sizeof(command.password), "%s",
             CONFIG_OTA_SENDER_WIFI_PASSWORD);

    bool success = comm_.sendOtaCommand(node_id, command);

    if (success) {
        ESP_LOGI(TAG, "OTA command sent successfully to node %u.", node_id);
    }
    else {
        ESP_LOGE(TAG, "Failed to send OTA command to node %u.", node_id);
    }
}

void OtaSenderApp::run()
{
    ESP_LOGI(TAG, "Starting Application Loop");
    while (true) {
        comm_.process();
        if (command_sent_) {
            ESP_LOGI(TAG, "OTA command has been sent. Entering idle state.");
            vTaskDelay(portMAX_DELAY); // Stop processing after sending the command
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
