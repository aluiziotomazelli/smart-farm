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
    config.wifi_channel       = 0; // Auto channel
    config.max_peers          = 10;
    config.heartbeat_interval = 0;
    config.ack_timeout        = 100;
    config.max_packet_size    = 250;

    if (!comm_.init(config)) {
        ESP_LOGE(TAG, "Failed to initialize ESP-NOW");
        return;
    }

    comm_.setPeerEventCallback(
        [this](const PeerInfo &peer, bool added) { this->onPeerEvent(peer, added); });

    comm_.setAckSuccessCallback([this](uint8_t node_id) { this->onAckSuccess(node_id); });

    comm_.startDiscovery(60000); // Discover indefinitely
    ESP_LOGI(TAG, "ESP-NOW initialized and discovery started. Our node ID: %u",
             comm_.get_id());
}

void OtaSenderApp::onAckSuccess(uint8_t node_id)
{
    ESP_LOGI(TAG, "ACK received from node %u", node_id);
}

void OtaSenderApp::onPeerEvent(const PeerInfo &peer, bool added)
{
    ESP_LOGI(TAG, "onPeerEvent callback fired. Node ID: %u, Added: %d", peer.node_id,
             added);
    if (added) {
        ESP_LOGI(TAG, "Peer found: %u. Queuing OTA command.", peer.node_id);
        peer_to_send_to_ = peer.node_id;
    }
}

void OtaSenderApp::sendOtaCommand(uint8_t node_id)
{
    OtaCommand command;
    snprintf(command.url, sizeof(command.url), "%s", CONFIG_OTA_SENDER_FIRMWARE_URL);
    snprintf(command.ssid, sizeof(command.ssid), "%s", CONFIG_OTA_SENDER_WIFI_SSID);
    snprintf(command.password, sizeof(command.password), "%s",
             CONFIG_OTA_SENDER_WIFI_PASSWORD);

    ESP_LOGI(TAG, "Sending OTA command to node %u...", node_id);

    bool success = comm_.sendOtaCommand(node_id, command);

    if (success) {
        ESP_LOGI(TAG, "OTA command sent successfully to node %u.", node_id);
    }
    else {
        ESP_LOGE(TAG, "Failed to send OTA command to node %u. Reason: %s", node_id,
                 comm_.getLastError());
    }
}

void OtaSenderApp::run()
{
    ESP_LOGI(TAG, "Starting Application Loop");
    while (true) {
        comm_.process();
        // vTaskDelay(pdMS_TO_TICKS(1000));
        peer_to_send_to_ = 146;
        if (peer_to_send_to_ != 0 && !command_sent_) {
            sendOtaCommand(peer_to_send_to_);
            peer_to_send_to_ = 0; // Clear the flag
            command_sent_    = true;
        }

        if (command_sent_) {
            ESP_LOGI(TAG, "OTA command has been sent. Entering idle state.");
            vTaskDelay(portMAX_DELAY); // Stop processing after sending the command
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
