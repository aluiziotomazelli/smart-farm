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
    , wifi_manager_(nullptr)
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
    }
    else {
        ESP_LOGW(TAG, "Received packet with unexpected size: %d bytes", len);
    }
}

void CentralHubApp::otaCleanupAndResume()
{
    wifi_manager_->disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));
    wifi_manager_->stop();
    vTaskDelay(pdMS_TO_TICKS(100));
    wifi_manager_->start();
    comm_.init(espnow_config_);
}

void CentralHubApp::registerEspNowCallbacks()
{
    comm_.setReceiveCallback(
        [this](uint8_t node_id, const uint8_t *data, int len, int8_t rssi) {
            this->on_espnow_receive(node_id, data, len, rssi);
        });

    comm_.setOtaCommandCallback([this](uint8_t node_id, const OtaCommand &command) {
        this->onOtaCommand(node_id, command);
    });
}

void CentralHubApp::onOtaCommand(uint8_t node_id, const OtaCommand &command)
{
    ESP_LOGI(TAG, "Received OTA command from node %u", node_id);
    ESP_LOGI(TAG, "URL: %s", command.url);
    ESP_LOGI(TAG, "SSID: %s", command.ssid);

    // Set flag to handle OTA in the main loop
    ota_command_received_  = true;
    received_ota_command_  = command;
    ota_command_sender_id_ = node_id;
}

void CentralHubApp::processOtaCommand()
{
    comm_.deinit();

    // Store credentials if provided
    if (strlen(received_ota_command_.ssid) > 0) {
        ESP_LOGI(TAG, "Storing new credentials for SSID: %s", received_ota_command_.ssid);
        wifi_manager_->storeCredentials(received_ota_command_.ssid,
                                        received_ota_command_.password);
    }

    std::string ssid, password;
    if (wifi_manager_->loadCredentials(ssid, password) != ESP_OK) {
        ESP_LOGE(TAG, "No credentials stored and none provided. Aborting OTA.");
        otaCleanupAndResume();
    }
    else {
        ESP_LOGI(TAG, "Connecting to Wi-Fi...");
        if (wifi_manager_->connect(ssid, password) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to connect to Wi-Fi. Resuming ESP-NOW.");
            otaCleanupAndResume();
        }
        else {
            ESP_LOGI(TAG, "Performing OTA...");
            esp_err_t ota_err =
                ota_manager_->performOtaWithMdns(received_ota_command_.url);
            // ota_manager_->performOta(received_ota_command_.url);
            if (ota_err != ESP_OK) {
                ESP_LOGE(TAG, "OTA failed. Cleaning up and resuming ESP-NOW.");
                otaCleanupAndResume();
            }
            // On success, the device will restart.
        }
    }
    // Reset the flag
    ota_command_received_ = false;
}

void CentralHubApp::init()
{
    ESP_LOGI(TAG, "Initializing CentralHubApp");

    wifi_manager_ = WiFiManager::getInstance();
    ota_manager_  = OtaManager::getInstance();

    ota_manager_->setDeviceType("central_hub");

    // One-time initialization of the network stack
    if (wifi_manager_->init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFiManager");
        return;
    }

    // Start Wi-Fi in STA mode for ESP-NOW
    if (wifi_manager_->start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFiManager");
        return;
    }

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
    espnow_config_.wifi_channel       = 0; // Auto channel
    espnow_config_.max_peers          = 10;
    espnow_config_.heartbeat_interval = 0;
    espnow_config_.ack_timeout        = 100;
    espnow_config_.max_packet_size    = 250;

    if (!comm_.init(espnow_config_)) {
        ESP_LOGE(TAG, "Failed to initialize ESP-NOW");
        return;
    }
    registerEspNowCallbacks();

    comm_.startDiscovery(60000);

    ESP_LOGI(TAG, "ESP-NOW initialized. Our node ID: %u", comm_.get_id());
}

void CentralHubApp::run()
{
    ESP_LOGW(TAG, "OTA bin 3");
    ESP_LOGI(TAG, "Starting Application Loop");

    while (true) {
        comm_.process();

        if (ota_command_received_) {
            processOtaCommand();
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // Small delay to prevent busy-waiting
    }
}
