#include "central_hub_app.hpp"
#include "esp_log.h"
#include "espnow.hpp"
#include "nvs_flash.h"
#include "ota_manager.hpp"
#include "protocol_types.hpp"
#include "wifi_manager.hpp"
#include <stdio.h>
#include <string.h>
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

    ESP_LOGI(TAG, "Main app task started. Press BOOT button to send OTA command.");

    bool button_pressed          = false;
    TickType_t last_press_time   = 0;
    const TickType_t debounce_ms = 150;
    uint32_t last_stack_check    = 0;

    while (true) {
        uint32_t notifications = 0;
        // Wait for notifications with a 20ms timeout to keep polling the button
        if (xTaskNotifyWait(0, NOTIFY_PEER_CHECK, &notifications, pdMS_TO_TICKS(20)) ==
            pdPASS) {
            if (notifications & NOTIFY_PEER_CHECK) {
                auto &espnow       = EspNow::instance();
                auto offline_peers = espnow.get_offline_peers();

                if (offline_peers.empty()) {
                    ESP_LOGI(TAG, "Peer check: All peers are online.");
                }
                else {
                    for (const auto &peer_id : offline_peers) {
                        ESP_LOGW(TAG, "Peer check: Peer with ID %u is offline.",
                                 static_cast<uint8_t>(peer_id));
                    }
                }
            }
        }

        // Drain app RX queue to prevent saturation
        EspNow::RxPacket rx_packet;
        while (xQueueReceive(app_queue_, &rx_packet, 0) == pdTRUE) {
            const MessageHeader *header =
                reinterpret_cast<const MessageHeader *>(rx_packet.data);
            ESP_LOGI(TAG, "App received packet from Node ID %u (MsgType: %u)",
                     static_cast<uint8_t>(header->sender_node_id),
                     static_cast<uint8_t>(header->msg_type));
        }

        // --- Button logic (polling) ---
        int level = gpio_get_level(BUTTON_GPIO);

        if (level == BUTTON_ACTIVE_LEVEL && !button_pressed) {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_press_time) > pdMS_TO_TICKS(debounce_ms)) {
                button_pressed  = true;
                last_press_time = now;

                ESP_LOGI(TAG, "Button pressed! Sending OTA command...");

                auto &espnow       = EspNow::instance();
                auto peers         = espnow.get_peers();
                NodeId target_node = NodeId(0);

                // Find the first non-hub peer
                for (const auto &peer : peers) {
                    if (peer.type != NodeType::HUB) {
                        target_node = peer.node_id;
                        break;
                    }
                }

                if (target_node != NodeId(0)) {
                    OtaCommand command = {};
                    snprintf(reinterpret_cast<char *>(command.firmware_url),
                             sizeof(command.firmware_url),
                             "http://ota-server.local:8070/ota_test.bin");

                    if (espnow.send_command(target_node, CommandType::START_OTA, &command,
                                            sizeof(command)) == ESP_OK) {
                        ESP_LOGI(TAG, "OTA command sent to node %u.",
                                 static_cast<uint8_t>(target_node));
                    }
                    else {
                        ESP_LOGE(TAG, "Failed to send OTA command to node %u.",
                                 static_cast<uint8_t>(target_node));
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

        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (now - last_stack_check > 5000) {
            last_stack_check = now;
            ESP_LOGI(TAG, "[Stack] app_main: %u bytes free",
                     (unsigned int)uxTaskGetStackHighWaterMark(NULL));
        }
    }
}

void CentralHubApp::peer_check_timer_cb(TimerHandle_t xTimer)
{
    CentralHubApp *app = static_cast<CentralHubApp *>(pvTimerGetTimerID(xTimer));
    if (app != nullptr && app->app_task_handle_ != nullptr) {
        xTaskNotify(app->app_task_handle_, NOTIFY_PEER_CHECK, eSetBits);
    }
}

void CentralHubApp::init()
{
    ESP_LOGI(TAG, "Initializing CentralHubApp");

    // Initialize NVS (required for WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

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

    // Create the application queue for ESP-NOW packets
    app_queue_ = xQueueCreate(10, sizeof(EspNow::RxPacket));
    if (app_queue_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create app_queue_");
        return;
    }

    EspNowConfig espnow_config;
    espnow_config.node_id       = NodeId::HUB;
    espnow_config.node_type     = NodeType::HUB;
    espnow_config.app_rx_queue  = app_queue_;
    espnow_config.is_master     = true;

    auto &espnow = EspNow::instance();
    if (espnow.init(espnow_config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ESP-NOW");
        return;
    }
    espnow.start_pairing();

    ESP_LOGI(TAG, "ESP-NOW initialized as HUB. Node ID: %u",
             static_cast<uint8_t>(espnow_config.node_id));

    xTaskCreate(button_task_handler, "app_main_task", 4096, this, 5, &app_task_handle_);

    peer_check_timer_handle_ = xTimerCreate("peer_check_timer", pdMS_TO_TICKS(10000),
                                            pdTRUE, this, peer_check_timer_cb);
    if (peer_check_timer_handle_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create peer_check_timer");
        return;
    }
    xTimerStart(peer_check_timer_handle_, 0);
}
