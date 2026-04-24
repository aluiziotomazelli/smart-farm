#include "water_tank_nvs.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"
#include "esp_timer.h"
#include <cstring>

static const char *TAG = "WaterTankNvs";

// --- Persistence in RTC Memory for Fill State Inference ---
// These variables retain their values across deep sleep cycles.
static RTC_DATA_ATTR uint16_t rtc_last_level_permille = 0;
static RTC_DATA_ATTR bool rtc_has_level               = false;

/**
 * @brief Infers the tank's fill state based on level changes over time.
 *
 * @param current_level The current water level in permille (0-1000).
 * @return The inferred FillState (FILLING, DRAINING, STABLE, or UNKNOWN).
 */
static FillState infer_fill_state(uint16_t current_level)
{
    if (!rtc_has_level) {
        rtc_last_level_permille = current_level;
        rtc_has_level           = true;
        return FillState::UNKNOWN;
    }

    int delta               = (int)current_level - (int)rtc_last_level_permille;
    rtc_last_level_permille = current_level;

    if (delta > +LEVEL_DELTA_MIN) {
        return FillState::FILLING;
    }
    if (delta < -LEVEL_DELTA_MIN) {
        return FillState::DRAINING;
    }

    return FillState::STABLE;
}

WaterTankNvs::WaterTankNvs()
    : NvsCore("water_tank")
{
}

/**
 * Updates the current tank status and accumulates sensor performance statistics.
 * This method centralizes data processing for the WaterTankApp.
 */
void WaterTankNvs::updateStatus(uint16_t  permille,
                                float     distance_cm,
                                UsQuality quality,
                                UsFailure failure)
{
    // 1. Update Real-time Data
    stats.last_distance_cm = distance_cm;
    stats.quality          = quality;
    stats.failure          = failure;
    stats.sample_uptime_s  = (uint32_t)(esp_timer_get_time() / 1000000ULL);

    // 2. Update Lifetime Statistics
    stats.measure_count++;

    // 3. Update counters and state based on quality
    switch (quality) {
    case UsQuality::OK:
        stats.ok_count++;
        stats.level_permille = permille;
        stats.fill_state     = infer_fill_state(permille);
        break;
    case UsQuality::WEAK:
        stats.weak_count++;
        stats.level_permille = permille;
        stats.fill_state     = infer_fill_state(permille);
        break;
    case UsQuality::INVALID:
    default:
        stats.invalid_count++;
        // Do not update level_permille on invalid read, keep the last known value
        stats.fill_state = FillState::UNKNOWN;
        break;
    }

    // 4. Track specific hardware failure types
    if (failure == UsFailure::TIMEOUT) {
        stats.timeout_count++;
    }
    else if (failure == UsFailure::HW_ERROR) {
        stats.hw_error_count++;
    }
    ESP_LOGD(TAG, "Stats updated: Total=%lu, OK=%lu, WEAK=%lu, State=%d",
             stats.measure_count, stats.ok_count, stats.weak_count,
             static_cast<int>(stats.fill_state));
}

/**
 * Hook implementation to load the specific WaterTankStats struct from NVS.
 */
esp_err_t WaterTankNvs::loadAppData()
{
    return loadStruct("tank_stats", stats);
}

/**
 * Hook implementation to save the specific WaterTankStats struct to NVS.
 */
esp_err_t WaterTankNvs::saveAppData()
{
    return saveStruct("tank_stats", stats);
}

/**
 * Hook implementation to define initial values for a fresh device.
 */
void WaterTankNvs::setAppDefaults()
{
    ESP_LOGI(TAG, "Setting application default values");

    // Reset the stats struct to zeros
    stats = {};

    // Define the specific Node Type for this device's common telemetry
    core_.node_type = NodeType::WATER_TANK;
}