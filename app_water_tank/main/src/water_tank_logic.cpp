#include "water_tank_logic.hpp"

WaterTankLogic::WaterTankLogic(const TankGeometry &geometry, const IFloatSwitch &float_switch)
    : geometry_(geometry), float_switch_(float_switch)
{
}

void WaterTankLogic::process_reading(float distance_cm, uint8_t quality, uint8_t failure, WaterTankStats &stats)
{
    stats.quality          = static_cast<UsQuality>(quality);
    stats.failure          = static_cast<UsFailure>(failure);
    stats.measure_count++;

    // Update counters
    switch (stats.quality) {
    case UsQuality::OK:
        stats.ok_count++;
        break;
    case UsQuality::WEAK:
        stats.weak_count++;
        break;
    case UsQuality::INVALID:
        stats.invalid_count++;
        if (stats.failure == UsFailure::TIMEOUT) {
            stats.timeout_count++;
        }
        break;
    }

    if (stats.quality != UsQuality::INVALID) {
        update_fill_state(distance_cm, stats);
        stats.last_distance_cm = distance_cm;
        stats.level_permille   = geometry_.calculate_permille(distance_cm);
    }
}

void WaterTankLogic::update_operation_mode(WaterTankStats &stats) const
{
    if (stats.quality == UsQuality::INVALID) {
        if (stats.consecutive_failures < CONSECUTIVE_FAILURES_THRESHOLD) {
            stats.consecutive_failures++;
        }
    }
    else if (stats.quality == UsQuality::OK) {
        if (stats.consecutive_failures > 0) {
            stats.consecutive_failures--;
        }
    }

    if (stats.consecutive_failures >= CONSECUTIVE_FAILURES_THRESHOLD) {
        stats.backup_mode_active = true;
    }
    else if (stats.consecutive_failures < CONSECUTIVE_FAILURES_THRESHOLD - 1) {
        stats.backup_mode_active = false;
    }
}

uint64_t WaterTankLogic::calculate_sleep_time_us(const WaterTankStats &stats) const
{
    if (stats.backup_mode_active) {
        if (!float_switch_.is_active()) {
            return BACKUP_MODE_SLEEP_US;
        }
        else {
            return TIMER_STABLE_US;
        }
    }

    uint64_t timer_us = 0;
    switch (stats.quality) {
    case UsQuality::OK:
    case UsQuality::WEAK:
        switch (stats.fill_state) {
        case FillState::FILLING:
            timer_us = TIMER_FILLING_US;
            break;
        case FillState::DRAINING:
            timer_us = TIMER_DRAIN_US;
            break;
        case FillState::STABLE:
            timer_us = TIMER_STABLE_US;
            break;
        default:
            timer_us = TIMER_UNKNOWN_US;
            break;
        }

        if (stats.quality == UsQuality::WEAK) {
            timer_us *= WEAK_SLEEP_FACTOR;
        }
        break;

    case UsQuality::INVALID:
    default:
        timer_us = TIMER_UNKNOWN_US * INVALID_SLEEP_FACTOR;
        break;
    }

    return timer_us;
}

void WaterTankLogic::update_fill_state(float distance_cm, WaterTankStats &stats) const
{
    if (stats.last_distance_cm == 0.0f) {
        stats.fill_state = FillState::STABLE;
        return;
    }

    float delta = distance_cm - stats.last_distance_cm;
    float abs_delta = (delta < 0) ? -delta : delta;

    // Convert cm delta to permille delta approximately for threshold check
    uint16_t permille_delta = geometry_.calculate_permille(stats.last_distance_cm) - 
                              geometry_.calculate_permille(distance_cm);
    
    // Using a simpler logic for now: if it changed more than LEVEL_DELTA_MIN
    if (abs_delta < 0.5f) { // Less than 5mm change is noise/stable
        stats.fill_state = FillState::STABLE;
    } else if (delta < 0) {
        stats.fill_state = FillState::FILLING;
    } else {
        stats.fill_state = FillState::DRAINING;
    }
}
