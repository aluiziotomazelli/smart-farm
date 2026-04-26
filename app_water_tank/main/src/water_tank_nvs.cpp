#include "water_tank_nvs.hpp"
#include "esp_log.h"
#include <cstring>

static const char *TAG = "WaterTankNvs";

WaterTankNvs::WaterTankNvs(IHalNvs &hal)
    : NvsCore("water_tank", hal)
{
}

esp_err_t WaterTankNvs::loadAppData()
{
    return loadStruct("tank_stats", stats);
}

esp_err_t WaterTankNvs::saveAppData()
{
    return saveStruct("tank_stats", stats);
}

void WaterTankNvs::setAppDefaults()
{
    ESP_LOGI(TAG, "Setting application default values");
    stats.reset();
    
    // Core identity defaults
    core_.node_id = static_cast<uint8_t>(FarmNodeId::WATER_TANK);
    core_.node_type = static_cast<uint8_t>(FarmNodeType::SENSOR);
}
