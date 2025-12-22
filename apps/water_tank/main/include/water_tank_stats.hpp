#pragma once

#include "UltrasonicSensor.hpp"
#include "core_types.hpp"
#include "water_tank_types.hpp"

struct WaterTankStats
{
    uint16_t  level_permille   = 0;
    FillState fill_state       = FillState::UNKNOWN;
    UsQuality quality          = UsQuality::INVALID;
    UsFailure failure          = UsFailure::NONE;
    float     last_distance_cm = 0.0f;
    uint32_t  sample_uptime_s  = 0;

    uint32_t measure_count  = 0;
    uint32_t ok_count       = 0;
    uint32_t weak_count     = 0;
    uint32_t invalid_count  = 0;
    uint32_t timeout_count  = 0;
    uint32_t hw_error_count = 0;

    // Wake / sleep
    bool gpio_wakeup_enabled = false;

    void reset()
    {
        *this = {};
    }
};
