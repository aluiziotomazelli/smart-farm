#pragma once
#include <cstdint>

/**
 * @class ILevelSensor
 * @brief Interface for a sensor that measures water level/distance.
 *
 * This abstraction allows the application to work with different sensor technologies
 * (Ultrasonic, Pressure, etc.) without changing the core logic.
 */
class ILevelSensor
{
public:
    virtual ~ILevelSensor() = default;

    /**
     * @brief Reads the raw distance or level from the sensor.
     *
     * @param[out] out_cm The measured distance in centimeters.
     * @param[out] out_quality A numeric representation of the reading quality (implementation specific).
     * @param[out] out_failure A numeric representation of any failure code (implementation specific).
     * @return true if the sensor was able to perform a measurement, false otherwise.
     */
    virtual bool read_raw_distance_cm(float &out_cm, uint8_t &out_quality, uint8_t &out_failure) = 0;
};
