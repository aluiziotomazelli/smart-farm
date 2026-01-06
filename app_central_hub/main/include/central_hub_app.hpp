#pragma once

#include "message_types.hpp"
#include <cstdint>

class CentralHubApp
{
public:
    CentralHubApp();

    void init();

private:
    void on_espnow_receive(uint8_t node_id, const uint8_t *data, int len, int8_t rssi);
    void registerEspNowCallbacks();

    static void button_task_handler(void *arg);
    void button_task();
};
