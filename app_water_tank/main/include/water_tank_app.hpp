#pragma once

#include "interfaces/i_level_sensor.hpp"
#include "interfaces/i_float_switch.hpp"
#include "interfaces/i_water_tank_storage.hpp"
#include "i_espnow_manager.hpp"
#include "water_tank_logic.hpp"
#include "water_tank_stats.hpp"

/**
 * @class WaterTankApp
 * @brief Orchestrator for the Water Tank monitoring application.
 *
 * This class implements the application flow using Dependency Injection.
 * It coordinates reading sensors, processing logic, and communicating results.
 */
class WaterTankApp
{
public:
    /**
     * @brief Construct a new Water Tank App.
     *
     * @param sensor Level sensor interface.
     * @param float_switch Float switch interface.
     * @param storage Storage interface.
     * @param comm Communication interface.
     * @param logic Business logic processor.
     */
    WaterTankApp(ILevelSensor &sensor, 
                 IFloatSwitch &float_switch, 
                 IWaterTankStorage &storage,
                 IEspNowManager &comm,
                 WaterTankLogic &logic);

    /**
     * @brief Main execution flow of the application.
     * 
     * Reads sensors, updates state, transmits data, and prepares for sleep.
     */
    void run();

private:
    ILevelSensor &sensor_;
    IFloatSwitch &float_switch_;
    IWaterTankStorage &storage_;
    IEspNowManager &comm_;
    WaterTankLogic &logic_;

    WaterTankStats stats_;

    /**
     * @brief Prepares and sends the water level report.
     */
    void send_report();

    /**
     * @brief Configures the deep sleep wakeup sources.
     * @param sleep_time_us Duration to sleep.
     */
    void enter_deep_sleep(uint64_t sleep_time_us);
};
