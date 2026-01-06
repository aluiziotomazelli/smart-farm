#include "central_hub_app.hpp"
#include "common_types.hpp"
#include "espnow_comm.hpp"
#include "ota_manager.hpp"
#include "wifi_manager.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "CentralHubApp";

// Button configuration
#define BUTTON_GPIO GPIO_NUM_0
#define BUTTON_ACTIVE_LEVEL 0 // BOOT button is active low

CentralHubApp::CentralHubApp()
{
    // Constructor is intentionally empty.
}

EspNowComm comm;

void CentralHubApp::on_espnow_receive(uint8_t node_id,
                                      const uint8_t *data,
                                      int len,
                                      int8_t rssi)
{
    ESP_LOGI(TAG, "Received %d bytes from node %u (RSSI: %d dBm)", len, node_id, rssi);

    if (len > 0 && data != nullptr) {
        auto header = reinterpret_cast<const MessageHeader *>(data);
        if (header->type == MessageType::DATA) {
            WaterLevelReport report;
            if (len == sizeof(report)) {
                memcpy(&report, data, sizeof(report));
                ESP_LOGI(
                    TAG,
                    "WaterLevelReport from node %u: Level=%u‰, Dist=%.2fcm, V=%.2fV, "
                    "Q=%d, F=%d, Full=%d, Backup=%d",
                    node_id, report.level_permille, report.distance_cm,
                    report.battery_mv / 1000.0f, (int)report.quality, (int)report.failure,
                    (int)report.float_switch_is_full, (int)report.backup_mode_active);
            }
            else {
                ESP_LOGW(TAG, "Received DATA packet with unexpected size: %d bytes", len);
            }
        }
        else {
            ESP_LOGI(TAG, "Received message of type %d", (int)header->type);
        }
    }
}

void CentralHubApp::registerEspNowCallbacks()
{
    comm.setReceiveCallback(
        [this](uint8_t node_id, const uint8_t *data, int len, int8_t rssi) {
            this->on_espnow_receive(node_id, data, len, rssi);
        });
}

void CentralHubApp::button_task_handler(void *arg)
{
    CentralHubApp *app = static_cast<CentralHubApp *>(arg);
    app->button_task();
}

void CentralHubApp::button_task()
{
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask  = (1ULL << BUTTON_GPIO);
    io_conf.mode          = GPIO_MODE_INPUT;
    io_conf.pull_up_en    = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type     = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);

    ESP_LOGI(TAG, "Button task started. Press BOOT button to send OTA command.");

    bool button_pressed          = false;
    TickType_t last_press_time   = 0;
    const TickType_t debounce_ms = 150;

    while (true) {
        int level = gpio_get_level(BUTTON_GPIO);

        if (level == BUTTON_ACTIVE_LEVEL && !button_pressed) {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_press_time) > pdMS_TO_TICKS(debounce_ms)) {
                button_pressed  = true;
                last_press_time = now;

                ESP_LOGI(TAG, "Button pressed! Sending OTA command...");

                auto peers          = comm.getPeers();
                uint8_t target_node = 0;

                // Find the first non-hub peer
                for (const auto &peer : peers) {
                    target_node = peer.node_id;
                    break;
                }

                if (target_node != 0) {
                    OtaCommand command = {};
                    // NOTE: The URL must be accessible by the target device.
                    // "ota-server.local" is a placeholder for mDNS.
                    snprintf(command.url, sizeof(command.url),
                             "http://ota-server.local:8070/ota_test.bin");

                    // Sending empty credentials will make the target use its stored ones.
                    command.ssid[0]     = '\0';
                    command.password[0] = '\0';

                    if (comm.send(target_node, MessageType::OTA, (uint8_t *)&command,
                                  sizeof(command)) == ESP_OK) {
                        ESP_LOGI(TAG, "OTA command sent to node %u.", target_node);
                    }
                    else {
                        ESP_LOGE(TAG, "Failed to send OTA command to node %u.",
                                 target_node);
                    }
                }
                else {
                    ESP_LOGW(TAG, "No suitable peer found to send OTA command to.");
                }
            }
        }
        else if (level != BUTTON_ACTIVE_LEVEL && button_pressed) {
            button_pressed = false; // Reset button state
        }

        vTaskDelay(pdMS_TO_TICKS(20)); // Poll every 20ms
    }
}

void CentralHubApp::init()
{
    ESP_LOGI(TAG, "Initializing CentralHubApp");

    auto &wifi = WiFiManager::instance();
    auto &ota  = OtaManager::instance();

    if (wifi.init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFiManager");
        return;
    }

    if (wifi.start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFiManager");
        return;
    }

    ota.init();

    ESPNOWConfig espnow_config;
    espnow_config.node_id   = common::generate_node_id();
    espnow_config.node_type = NodeType::HUB;

    if (!comm.init(espnow_config)) {
        ESP_LOGE(TAG, "Failed to initialize ESP-NOW");
        return;
    }

    registerEspNowCallbacks();

    comm.startDiscovery(50000); // Infinite discovery

    ESP_LOGI(TAG, "ESP-NOW initialized as HUB. Node ID: %u", espnow_config.node_id);

    xTaskCreate(button_task_handler, "button_task", 4096, this, 5, NULL);
}
