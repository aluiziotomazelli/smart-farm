#pragma once

#include "interfaces/i_level_sensor.hpp"
#include "interfaces/i_water_tank_storage.hpp"
#include "i_espnow_manager.hpp"
#include "water_tank_logic.hpp"
#include "water_tank_stats.hpp"
#include "i_float_switch.hpp" // Adding component include

/**
 * @class WaterTankApp
 * @brief Orchestrator for the Water Tank monitoring application.
 */
class WaterTankApp
{
public:
    WaterTankApp(
        ILevelSensor& sensor,
        floatswitch::IFloatSwitch& float_switch,
        IWaterTankStorage& storage,
        espnow::IEspNowManager& comm,
        WaterTankLogic& logic);

    void run();

private:
    ILevelSensor& sensor_;
    floatswitch::IFloatSwitch& float_switch_;
    IWaterTankStorage& storage_;
    espnow::IEspNowManager& comm_;
    WaterTankLogic& logic_;

    WaterTankStats stats_;

    void send_report();
    void enter_deep_sleep(uint64_t sleep_time_us);
};
