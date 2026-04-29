#include "water_tank_app.hpp"
#include "esp_log.h"

static const char* TAG = "main";

extern "C" void app_main()
{
    ESP_LOGI(TAG, "Initializing Smart Farm Water Tank...");

    // The default constructor prepares the production stack (uninitialized)
    static WaterTankApp app;
    
    // init() instantiates and wires up all the hardware drivers
    app.init();
    
    // run() starts the main application flow
    app.run();
}
