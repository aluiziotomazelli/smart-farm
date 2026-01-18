#pragma once

#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

class CentralHubApp
{
public:
    CentralHubApp();

    void init();

private:
    static void button_task_handler(void *arg);
    void button_task();

    static void peer_check_timer_cb(TimerHandle_t xTimer);
    TimerHandle_t peer_check_timer_handle_ = nullptr;
};
