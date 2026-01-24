#include "app_slave.hpp"
#include "esp_log.h"
#include "espnow.hpp"
#include "protocol_types.hpp"
#include "wifi_manager.hpp"

static const char *TAG = "AppSlave";

AppSlave::AppSlave()
{
}
AppSlave::~AppSlave()
{
}

AppSlave &AppSlave::instance()
{
    static AppSlave instance;
    return instance;
}

void AppSlave::init()
{
    ESP_LOGI(TAG, "Initializing AppSlave...");

    auto &wifi = WiFiManager::instance();
    if (wifi.init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFiManager");
        return;
    }
    if (wifi.start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFiManager");
        return;
    }

    app_queue_ = xQueueCreate(10, sizeof(EspNow::RxPacket));
    if (app_queue_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create app_queue_");
        return;
    }

    EspNowConfig config;
    config.node_id                     = NodeId::WATER_TANK;
    config.node_type                   = NodeType::SENSOR;
    config.heartbeat_interval_ms       = 10000;
    config.app_rx_queue                = app_queue_;
    config.stack_size_transport_worker = 3584; // Optimized for Slave (Safe margin)

    auto &espnow = EspNow::instance();
    espnow.init(config);
}

void AppSlave::run()
{
    ESP_LOGI(TAG, "Starting AppSlave...");
    auto &espnow = EspNow::instance();
    espnow.start_pairing();

    EspNow::RxPacket packet;
    while (true) {
        // Drain incoming messages
        while (xQueueReceive(app_queue_, &packet, 0) == pdTRUE) {
            ESP_LOGI(TAG, "Received packet in application queue.");
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "Running AppSlave...");
    }
}
