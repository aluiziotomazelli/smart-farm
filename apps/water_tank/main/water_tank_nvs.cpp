#include "water_tank_nvs.hpp"

#include "esp_log.h"
#include "esp_timer.h"
#include <cstring>

static const char *TAG = "WaterTankNvs";

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
    esp_err_t err;

    err = nvs_get_u16(_handle, "level_permille", &stats.level_permille);
    if (err != ESP_OK)
        return err;

    int8_t fill_state_i8;
    err = nvs_get_i8(_handle, "fill_state", &fill_state_i8);
    if (err != ESP_OK)
        return err;
    stats.fill_state = static_cast<FillState>(fill_state_i8);

    int8_t quality_i8;
    err = nvs_get_i8(_handle, "quality", &quality_i8);
    if (err != ESP_OK)
        return err;
    stats.quality = static_cast<UsQuality>(quality_i8);

    int8_t failure_i8;
    err = nvs_get_i8(_handle, "failure", &failure_i8);
    if (err != ESP_OK)
        return err;
    stats.failure = static_cast<UsFailure>(failure_i8);

    uint32_t distance_cm_u32;
    err = nvs_get_u32(_handle, "last_dist_cm", &distance_cm_u32);
    if (err != ESP_OK)
        return err;
    memcpy(&stats.last_distance_cm, &distance_cm_u32, sizeof(stats.last_distance_cm));

    err = nvs_get_u32(_handle, "sample_uptime", &stats.sample_uptime_s);
    if (err != ESP_OK)
        return err;

    err = nvs_get_u32(_handle, "measure_count", &stats.measure_count);
    if (err != ESP_OK)
        return err;

    err = nvs_get_u32(_handle, "ok_count", &stats.ok_count);
    if (err != ESP_OK)
        return err;

    err = nvs_get_u32(_handle, "weak_count", &stats.weak_count);
    if (err != ESP_OK)
        return err;

    err = nvs_get_u32(_handle, "invalid_count", &stats.invalid_count);
    if (err != ESP_OK)
        return err;

    err = nvs_get_u32(_handle, "timeout_count", &stats.timeout_count);
    if (err != ESP_OK)
        return err;

    err = nvs_get_u32(_handle, "hw_error_count", &stats.hw_error_count);
    if (err != ESP_OK)
        return err;

    uint8_t gpio_wakeup;
    err = nvs_get_u8(_handle, "gpio_wakeup", &gpio_wakeup);
    if (err != ESP_OK)
        return err;
    stats.gpio_wakeup_enabled = (gpio_wakeup == 1);

    return ESP_OK; // Return OK if all reads succeeded
}

/**
 * Hook implementation to save the specific WaterTankStats struct to NVS.
 */
esp_err_t WaterTankNvs::saveAppData()
{
    esp_err_t err;

    // Métodos `nvs_set_*` não precisam de handle, a classe base gerencia.
    // O handle é passado internamente pelo `_handle` da classe NvsCore.
    // Agrupando as chamadas para melhor performance (menos commits)
    err = nvs_set_u16(_handle, "level_permille", stats.level_permille);
    if (err != ESP_OK)
        return err;

    err = nvs_set_i8(_handle, "fill_state", static_cast<int8_t>(stats.fill_state));
    if (err != ESP_OK)
        return err;

    err = nvs_set_i8(_handle, "quality", static_cast<int8_t>(stats.quality));
    if (err != ESP_OK)
        return err;

    err = nvs_set_i8(_handle, "failure", static_cast<int8_t>(stats.failure));
    if (err != ESP_OK)
        return err;

    // Para float, precisamos converter para u32
    uint32_t distance_cm_u32;
    memcpy(&distance_cm_u32, &stats.last_distance_cm, sizeof(distance_cm_u32));
    err = nvs_set_u32(_handle, "last_dist_cm", distance_cm_u32);
    if (err != ESP_OK)
        return err;

    err = nvs_set_u32(_handle, "sample_uptime", stats.sample_uptime_s);
    if (err != ESP_OK)
        return err;

    err = nvs_set_u32(_handle, "measure_count", stats.measure_count);
    if (err != ESP_OK)
        return err;

    err = nvs_set_u32(_handle, "ok_count", stats.ok_count);
    if (err != ESP_OK)
        return err;

    err = nvs_set_u32(_handle, "weak_count", stats.weak_count);
    if (err != ESP_OK)
        return err;

    err = nvs_set_u32(_handle, "invalid_count", stats.invalid_count);
    if (err != ESP_OK)
        return err;

    err = nvs_set_u32(_handle, "timeout_count", stats.timeout_count);
    if (err != ESP_OK)
        return err;

    err = nvs_set_u32(_handle, "hw_error_count", stats.hw_error_count);
    if (err != ESP_OK)
        return err;

    uint8_t gpio_wakeup = stats.gpio_wakeup_enabled ? 1 : 0;
    err                 = nvs_set_u8(_handle, "gpio_wakeup", gpio_wakeup);

    return err;
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