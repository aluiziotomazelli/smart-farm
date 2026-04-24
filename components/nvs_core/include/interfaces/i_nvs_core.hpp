#pragma once

#include "esp_err.h"

/**
 * @brief Interface for the NVS Core storage manager.
 * 
 * Provides a standardized way to initialize, load, and commit system and 
 * application data to Non-Volatile Storage.
 */
class INvsCore {
public:
    virtual ~INvsCore() = default;

    /**
     * @brief Initializes the NVS partition.
     */
    virtual esp_err_t init_partition() = 0;

    /**
     * @brief Loads core and application data from NVS.
     */
    virtual esp_err_t load() = 0;

    /**
     * @brief Commits core and application data to NVS.
     */
    virtual esp_err_t commit() = 0;

    /**
     * @brief Resets the namespace to default values.
     */
    virtual void factory_reset() = 0;

    /**
     * @brief Erases the entire namespace.
     */
    virtual esp_err_t erase_namespace() = 0;
};
