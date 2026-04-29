// app_water_tank/main/src/tank_geometry.cpp

#include "tank_geometry.hpp"
#include "esp_log.h"
#include <cstdint>

static const char* TAG = "TankGeometry";

uint16_t TankGeometry::calculate_permille(float distance_cm) const
{
    // Convert distance sensor to water height
    uint16_t dist_cm_round = static_cast<uint16_t>(distance_cm + 0.5f);

    if (dist_cm_round <= offset_cm_) {
        return 1000; // water level is above the offset
    }

    uint16_t water_height = dist_cm_round - offset_cm_;

    if (water_height >= height_cm_) {
        return 0; // empty tank
    }

    // Lookup table interpolation
    return height_to_permille(static_cast<uint8_t>(water_height));
}

// =====================================================================
// Private methods
// =====================================================================

uint16_t TankGeometry::height_to_permille(uint8_t height_cm) const
{
    static constexpr uint8_t SEGMENT_HEIGHT = 30;

    // Determine which segment the height belongs to
    uint8_t segment = height_cm / SEGMENT_HEIGHT; // 0-4
    uint8_t offset = height_cm % SEGMENT_HEIGHT;  // 0-29

    uint16_t p0 = VOLUME_LUT[segment];
    uint16_t p1 = VOLUME_LUT[segment + 1];

    return p0 - ((p0 - p1) * offset) / SEGMENT_HEIGHT;
}