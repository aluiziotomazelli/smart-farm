#include "central_hub_app.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" void app_main()
{
    CentralHubApp app;
    app.init();

    // The main logic now runs in FreeRTOS tasks.
    // This loop keeps the main task alive.
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
