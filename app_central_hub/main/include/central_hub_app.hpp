#pragma once

#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

class CentralHubApp
{
public:
    CentralHubApp();

    void init();

private:
    static constexpr uint32_t NOTIFY_PEER_CHECK = 0x01;

    static void button_task_handler(void *arg);
    void button_task();

    static void peer_check_timer_cb(TimerHandle_t xTimer);
    TimerHandle_t peer_check_timer_handle_ = nullptr;
    TaskHandle_t app_task_handle_          = nullptr;
};
