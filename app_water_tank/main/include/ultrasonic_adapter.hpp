#pragma once
#include "interfaces/i_level_sensor.hpp"
#include "us_sensor.hpp" // Use the header from managed components

/**
 * @class UltrasonicLevelSensorAdapter
 * @brief Adapter for the UltrasonicSensor component to match ILevelSensor interface.
 */
class UltrasonicLevelSensorAdapter : public ILevelSensor
{
public:
    UltrasonicLevelSensorAdapter(ultrasonic::UsSensor &sensor) : sensor_(sensor) {}

    bool read_raw_distance_cm(float &out_cm, uint8_t &out_quality, uint8_t &out_failure) override
    {
        ultrasonic::UsResult result = sensor_.read_distance_cm(out_cm);
        
        // Map UsResult to numeric quality/failure for the interface
        if (ultrasonic::is_success(result)) {
            out_quality = (result == ultrasonic::UsResult::OK) ? 1 : 2; // 1=OK, 2=WEAK
            out_failure = 0; // NONE
            return true;
        } else {
            out_quality = 0; // INVALID
            out_failure = static_cast<uint8_t>(result);
            return false;
        }
    }

private:
    ultrasonic::UsSensor &sensor_;
};
