

#include "water_tank_app.hpp"
#include "comm_espnow.hpp"

#include "esp_log.h"

extern "C" void app_main()
{
    WaterTankApp app;
    app.attach_comm(&comm::CommEspNow::instance());
    app.init();
    app.run();
}