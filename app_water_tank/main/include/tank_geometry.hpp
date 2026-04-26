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
     * @param min_distance_cm Distance when the tank is EMPTY (0 permille).
     * @param max_distance_cm Distance when the tank is FULL (1000 permille).
     */
    TankGeometry(float min_distance_cm, float max_distance_cm)
        : min_dist_(min_distance_cm), max_dist_(max_distance_cm)
    {
    }

    /**
     * @brief Converts a distance reading into volume in permille (0-1000).
     *
     * @param distance_cm The raw distance reading from the sensor.
     * @return uint16_t The volume in permille (‰).
     */
    uint16_t calculate_permille(float distance_cm) const
    {
        if (distance_cm >= min_dist_) {
            return 0;
        }
        if (distance_cm <= max_dist_) {
            return 1000;
        }

        // Linear interpolation for a cylindrical tank
        float span  = min_dist_ - max_dist_;
        float level = (min_dist_ - distance_cm) / span;

        return static_cast<uint16_t>(level * 1000.0f);
    }

private:
    const float min_dist_; ///< Distance for empty tank (cm).
    const float max_dist_; ///< Distance for full tank (cm).
};
