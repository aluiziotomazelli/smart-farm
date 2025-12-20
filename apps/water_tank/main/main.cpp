

#include "WaterTankApp.hpp"

#include "esp_log.h"

extern "C" void app_main()
{
    WaterTankApp app;
    app.init();
    app.run();
}