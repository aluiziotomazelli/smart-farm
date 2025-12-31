#pragma once

#include "esp_now.h"
#include "espnow_comm.hpp"
#include "nvs_core.hpp"
#include "ota_manager.hpp"
#include "common_types.hpp"

class CentralHubApp
{
public:
    CentralHubApp();

    void init();
    void run();

private:
    void on_espnow_receive(uint8_t node_id, const uint8_t *data, int len, int8_t rssi);
    void onOtaCommand(uint8_t node_id, const OtaCommand &command);

    // NvsCore storage_;
    EspNowComm comm_;
    OtaManager *ota_manager_;
};
