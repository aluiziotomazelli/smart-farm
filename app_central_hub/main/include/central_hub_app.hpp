#pragma once

#include "esp_now.h"
#include "espnow_comm.hpp"
#include "nvs_core.hpp"

class CentralHubApp
{
public:
    CentralHubApp();

    void init();
    void run();

private:
    void on_espnow_receive(uint8_t node_id, const uint8_t *data, int len, int8_t rssi);

    // NvsCore storage_;
    EspNowComm comm_;
};
