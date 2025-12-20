#include "WaterTankNvs.hpp"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "WaterTankNvs";

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
    stats.level_permille   = permille;
    stats.last_distance_cm = distance_cm;
    stats.quality          = quality;
    stats.failure          = failure;

    // Store exact measurement timestamp
    stats.sample_uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);

    // 2. Increment Lifetime Statistics
    stats.measure_count++;

    // Quality stats with explicit breaks to prevent fall-through
    switch (quality)
    {
    case UsQuality::OK:
        stats.ok_count++;
        break;
    case UsQuality::WEAK:
        stats.weak_count++;
        break;
    case UsQuality::INVALID:
    default:
        stats.invalid_count++;
        break;
    }

    // Hardware failure tracking
    if (failure == UsFailure::TIMEOUT)
    {
        stats.timeout_count++;
    }
    else if (failure == UsFailure::HW_ERROR)
    {
        stats.hw_error_count++;
    }

    ESP_LOGD(TAG, "Stats updated: Total=%lu, OK=%lu, WEAK=%lu", stats.measure_count, stats.ok_count,
             stats.weak_count);
}

/**
 * Hook implementation to load the specific WaterTankStats struct from NVS.
 */
esp_err_t WaterTankNvs::loadAppData()
{
    // Loads the blob stored under the key "tank_stats" into the stats struct
    return loadStruct("tank_stats", stats);
}

/**
 * Hook implementation to save the specific WaterTankStats struct to NVS.
 */
esp_err_t WaterTankNvs::saveAppData()
{
    // Saves the stats struct as a blob under the key "tank_stats"
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