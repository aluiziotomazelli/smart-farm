#pragma once

#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

class AppSlave
{
public:
    static AppSlave &instance();
    void init();
    void run();

private:
    AppSlave();
    ~AppSlave();
    AppSlave(const AppSlave &)            = delete;
    AppSlave &operator=(const AppSlave &) = delete;

    QueueHandle_t app_queue_ = nullptr;
};
