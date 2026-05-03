#pragma once

#include "esp_err.h"
#include "esp_app_desc.h"
#include "esp_partition.h"

/**
 * @brief Interface for accessing system and partition information.
 */
class ISystem {
public:
    virtual ~ISystem() = default;

    /** @copydoc esp_restart() */
    virtual void restart() = 0;

    /** @copydoc esp_ota_get_app_description() */
    virtual const esp_app_desc_t* get_running_app_desc() = 0;

    /** @copydoc esp_partition_get_sha256() */
    virtual esp_err_t get_partition_sha256(const esp_partition_t* partition, uint8_t* sha_256) = 0;

    /** @brief Returns the next OTA update partition. */
    virtual const esp_partition_t* get_update_partition() = 0;
};
