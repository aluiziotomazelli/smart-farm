#pragma once

#include "nvs_core.hpp"
#include "water_tank_stats.hpp"

/**
 * @class WaterTankNvs
 * @brief Persistent storage handler for the Water Tank application.
 *
 * Inherits from NvsCore to provide structured storage for WaterTankStats.
 */
class WaterTankNvs : public NvsCore
{
public:
    WaterTankNvs();
    
    /**
     * @brief The current statistics/state loaded from or to be saved to NVS.
     */
    WaterTankStats stats;

protected:
    /** @copydoc NvsCore::loadAppData() */
    esp_err_t loadAppData() override;

    /** @copydoc NvsCore::saveAppData() */
    esp_err_t saveAppData() override;

    /** @copydoc NvsCore::setAppDefaults() */
    void setAppDefaults() override;
};
