#include "central_hub_app.hpp"
#include "comm_interface.hpp"
#include "protocol_frame.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

static const char *TAG = "CentralHubApp";

CentralHubApp& CentralHubApp::instance()
{
    static CentralHubApp s_instance;
    return s_instance;
}

void CentralHubApp::init()
{
    ESP_LOGI(TAG, "Initializing communication component");
    auto& comm = comm::CommInterface::get_default_instance();
    if (!comm.init()) {
        ESP_LOGE(TAG, "Failed to initialize communication component");
        return;
    }
    comm.start();
}

void CentralHubApp::run()
{
    ESP_LOGI(TAG, "Central Hub is running, waiting for messages...");
    auto& comm = comm::CommInterface::get_default_instance();
    comm::protocol::Frame frame{};

    while (true) {
        if (comm.receive(frame)) {
            ESP_LOGI(TAG, "Message received from node 0x%08X", frame.header.node_id);
            if (frame.header.type == comm::protocol::MessageType::DATA && frame.payload_len == sizeof(uint16_t)) {
                uint16_t nivel_permile;
                memcpy(&nivel_permile, frame.payload, sizeof(nivel_permile));
                ESP_LOGI(TAG, "Received water level: %d permile", nivel_permile);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // Check for messages every 100ms
    }
}
