#include "nvs_core.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"
#include <cstring>

static const char* TAG = "NvsCore";

NvsCore::NvsCore(const char* ns, IHalNvs& hal)
    : hal_(hal)
    , _namespace(ns)
{
    // Ensure core data is initialized to zero
    memset(&core_, 0, sizeof(CoreStorage));
}

esp_err_t NvsCore::init_partition()
{
    esp_err_t err = hal_.hal_nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition invalid, erasing");
        hal_.hal_nvs_flash_erase();
        err = hal_.hal_nvs_flash_init();
    }
    return err;
}

esp_err_t NvsCore::open_nvs(nvs_open_mode_t mode)
{
    if (_isOpen)
        return ESP_OK; // Already open
    esp_err_t err = hal_.hal_nvs_open(_namespace, mode, &_handle);
    if (err == ESP_OK)
        _isOpen = true;
    return err;
}

void NvsCore::close_nvs()
{
    if (_isOpen) {
        hal_.hal_nvs_close(_handle);
        _isOpen = false;
    }
}

esp_err_t NvsCore::load()
{
    esp_err_t err = open_nvs(NVS_READONLY);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for reading: %s", esp_err_to_name(err));
        return err;
    }

    // 1. Load Core Data
    err = loadStruct("core_data", core_);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Core data not found or corrupt: %s", esp_err_to_name(err));
        close_nvs();
        return err;
    }

    // Core Schema Validation
    if (core_.schema_version != CORE_SCHEMA_VERSION) {
        ESP_LOGW(TAG, "Schema mismatch. Migrating...");
        // Migration or reset logic...
        core_.schema_version = CORE_SCHEMA_VERSION;
    }

    // 2. Call derived class to load application data
    err = loadAppData();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "App data not found or corrupt: %s", esp_err_to_name(err));
        // Don't reset here, just propagate error
    }

    close_nvs();
    return err; // Return final status (could be an app error)
}

esp_err_t NvsCore::commit()
{
    esp_err_t err = open_nvs(NVS_READWRITE);
    if (err != ESP_OK)
        return err;

    // 1. Save Core
    err = saveStruct("core_data", core_);
    if (err != ESP_OK) {
        close_nvs();
        return err;
    }

    // 2. Call derived class to save application data
    err = saveAppData();

    // 3. Final commit to flash
    if (err == ESP_OK) {
        err = hal_.hal_nvs_commit(_handle);
    }

    close_nvs();
    return err;
}

void NvsCore::apply_core_defaults()
{
    memset(&core_, 0, sizeof(CoreStorage));
    core_.schema_version = CORE_SCHEMA_VERSION;
    core_.node_type = FarmNodeType::UNKNOWN; 
    core_.node_id = FarmNodeId(0);
    core_.hw_revision = 1;
    core_.fw_major = 0;
    core_.fw_minor = 1;
    core_.fw_patch = 0;
    core_.boot_count = 0;
    core_.crash_count = 0;
    core_.has_valid_time = false;
    core_.unix_time = 0;
    core_.last_sync_uptime = 0;
    core_.power_profile = PowerProfile::ALWAYS_ON;
    core_.sleep_interval_s = 3600;
    core_.last_wake = WakeSource::POWER_ON;
}

void NvsCore::factory_reset()
{
    erase_namespace(); // Erase all first
    apply_core_defaults();
    setAppDefaults();
    commit(); // Save default values
}

esp_err_t NvsCore::erase_namespace()
{
    esp_err_t err = open_nvs(NVS_READWRITE);
    if (err != ESP_OK)
        return err;

    err = hal_.hal_nvs_erase_all(_handle);
    if (err == ESP_OK) {
        err = hal_.hal_nvs_commit(_handle);
    }

    close_nvs();
    return err;
}