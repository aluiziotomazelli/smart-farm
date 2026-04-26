#pragma once
#include "interfaces/i_water_tank_storage.hpp"
#include "water_tank_nvs.hpp"

/**
 * @class WaterTankStorageAdapter
 * @brief Adapter for WaterTankNvs to match IWaterTankStorage interface.
 */
class WaterTankStorageAdapter : public IWaterTankStorage
{
public:
    WaterTankStorageAdapter(WaterTankNvs &nvs) : nvs_(nvs) {}

    esp_err_t load(WaterTankStats &stats) override
    {
        esp_err_t err = nvs_.loadAppData();
        if (err == ESP_OK) {
            stats = nvs_.stats;
        }
        return err;
    }

    esp_err_t save(const WaterTankStats &stats) override
    {
        nvs_.stats = stats;
        return nvs_.saveAppData();
    }

    void reset_to_defaults(WaterTankStats &stats) override
    {
        nvs_.setAppDefaults();
        stats = nvs_.stats;
    }

private:
    WaterTankNvs &nvs_;
};
