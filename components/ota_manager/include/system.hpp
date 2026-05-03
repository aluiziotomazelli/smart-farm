#pragma once

#include "interfaces/i_system.hpp"
#include "esp_system.h"
#include "esp_ota_ops.h"

/**
 * @brief Concrete implementation of ISystem.
 */
class System : public ISystem
{
public:
    /** @copydoc ISystem::restart */
    void restart() override { esp_restart(); }

    /** @copydoc ISystem::get_running_app_desc */
    const esp_app_desc_t* get_running_app_desc() override { return esp_ota_get_app_description(); }

    /** @copydoc ISystem::get_partition_sha256 */
    esp_err_t get_partition_sha256(const esp_partition_t* partition, uint8_t* sha_256) override
    {
        return esp_partition_get_sha256(partition, sha_256);
    }

    /** @copydoc ISystem::get_update_partition */
    const esp_partition_t* get_update_partition() override { return esp_ota_get_next_update_partition(NULL); }
};
