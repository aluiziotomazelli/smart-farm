#pragma once

#include "interfaces/i_float_switch.hpp"
#include "tank_geometry.hpp"
#include "water_tank_stats.hpp"
#include "water_tank_types.hpp"

/**
 * @class WaterTankLogic
 * @brief The core business logic of the water tank system.
 *
 * This class is responsible for evaluating sensor data, updating the system state
 * (Backup Mode, Fill State), and deciding operational parameters like sleep duration.
 * It is decoupled from hardware and OS specificities.
 */
class WaterTankLogic
{
public:
    /**
     * @brief Construct a new Water Tank Logic object.
     *
     * @param geometry Reference to the tank geometry calculator.
     * @param float_switch Reference to the float switch interface.
     */
    WaterTankLogic(const TankGeometry &geometry, const IFloatSwitch &float_switch);

    /**
     * @brief Processes a new sensor reading and updates the provided statistics.
     *
     * @param distance_cm Raw distance reading from sensor.
     * @param quality Quality indicator of the reading.
     * @param failure Failure code if any.
     * @param stats The stats struct to be updated.
     */
    void process_reading(float distance_cm, uint8_t quality, uint8_t failure, WaterTankStats &stats);

    /**
     * @brief Calculates the recommended deep sleep duration.
     *
     * @param stats Current system statistics.
     * @return uint64_t Sleep duration in microseconds.
     */
    uint64_t calculate_sleep_time_us(const WaterTankStats &stats) const;

    /**
     * @brief Updates the operational mode (Normal vs Backup) based on current errors.
     *
     * @param stats Current system statistics.
     */
    void update_operation_mode(WaterTankStats &stats) const;

private:
    const TankGeometry &geometry_;
    const IFloatSwitch &float_switch_;

    /**
     * @brief Internal helper to determine fill state based on reading trends.
     */
    void update_fill_state(float distance_cm, WaterTankStats &stats) const;
};
