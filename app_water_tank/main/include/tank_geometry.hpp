#pragma once
#include <cstdint>
#include <algorithm>

/**
 * @class TankGeometry
 * @brief Handles mathematical conversions for water tank measurements.
 *
 * This class is responsible for converting raw physical measurements (distance)
 * into application-specific metrics (permille volume).
 *
 * @note This implementation assumes a cylindrical tank (linear volume-to-height relationship).
 */
class TankGeometry
{
public:
    /**
     * @brief Construct a new Tank Geometry object.
     *
     * @param tank_height_cm The total height of the tank (cm).
     * @param sensor_offset_cm Distance from sensor to water surface when tank is FULL.
     *                         This calibrates the sensor mounting position above the tank.
     */
    TankGeometry(uint8_t tank_height_cm, uint8_t sensor_offset_cm)
        : height_cm_(tank_height_cm)
        , offset_cm_(sensor_offset_cm)
    {
    }

    /**
     * @brief Convert sensor distance reading to water level permille (0-1000).
     *
     * @param distance_cm Raw distance reading from ultrasonic sensor.
     * @return uint16_t Volume in permille (0 = empty, 1000 = full), clamped to valid range.
     */
    uint16_t calculate_permille(float distance_cm) const;

private:
    /**
     * @brief Volume lookup table (distance vs permille)
     * @note This table is used to convert the distance reading into volume in permille (0-1000).
     */
    static constexpr uint16_t VOLUME_LUT[6] = {
        1000, // top of the tank
        751,  // edge of step 5/4
        528,  // edge of step 4/3
        329,  // edge of step 3/2
        153,  // edge of step 2/1
        0     // bottom of the tank
    };

    const uint8_t height_cm_; ///< Height of the tank (cm).
    const uint8_t offset_cm_; ///< Offset of the sensor from the top of water max level (cm).

    /**
     * @brief Convert water height to permille using LUT interpolation.
     *
     * @param height_cm Water height in cm. Must be in range [1, height_cm_-1].
     * @note Caller is responsible for range validation.
     * @return uint16_t Volume in permille (0-1000).
     */
    uint16_t height_to_permille(uint8_t height_cm) const;
};
