#pragma once

#include "esp_now.h"
#include "espnow_comm.hpp"
#include "message_types.hpp"
#include "nvs_core.hpp"
#include "ota_manager.hpp"
#include "wifi_manager.hpp"

class CentralHubApp
{
public:
    CentralHubApp();

    void init();
    void run();

private:
    void on_espnow_receive(uint8_t node_id, const uint8_t *data, int len, int8_t rssi);
    void onOtaCommand(uint8_t node_id, const OtaCommand &command);
    void processOtaCommand();
    void otaCleanupAndResume();

    void registerEspNowCallbacks();

    EspNowComm comm_;
    OtaManager *ota_manager_;
    WiFiManager *wifi_manager_;
    ESPNOWConfig espnow_config_;

    bool ota_command_received_;
    OtaCommand received_ota_command_;
    uint8_t ota_command_sender_id_;
};
