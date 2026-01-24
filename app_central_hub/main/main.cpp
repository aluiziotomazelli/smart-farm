#include "central_hub_app.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" void app_main()
{
    CentralHubApp app;
    app.init();
    app.run();
}
