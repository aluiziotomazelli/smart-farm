// include/interfaces/i_hal_nvs.hpp
#pragma once

#include "esp_err.h"
#include "nvs.h"

/**
 * @class IHalNvs
 * @brief Interface for the NVS Hardware Abstraction Layer.
 *
 * This interface defines the contract for NVS operations, allowing
 * them to be mocked for host-based testing.
 */
class IHalNvs
{
public:
    virtual ~IHalNvs() = default;

    /**
     * @brief Initialize the default NVS partition.
     * @copydoc nvs_flash_init()
     */
    virtual esp_err_t hal_nvs_flash_init() = 0;

    /**
     * @brief Erase the default NVS partition.
     * @copydoc nvs_flash_erase()
     */
    virtual esp_err_t hal_nvs_flash_erase() = 0;

    /**
     * @brief Erase all keys and values in a namespace.
     * @copydoc nvs_erase_all()
     */
    virtual esp_err_t hal_nvs_erase_all(nvs_handle_t handle) = 0;

    /**
     * @brief Open a namespace for NVS operations.
     * @copydoc nvs_open()
     */
    virtual esp_err_t hal_nvs_open(const char *name, nvs_open_mode_t open_mode, nvs_handle_t *out_handle) = 0;

    /**
     * @brief Close an NVS handle.
     * @copydoc nvs_close()
     */
    virtual void hal_nvs_close(nvs_handle_t handle) = 0;

    /**
     * @brief Write a blob to NVS.
     * @copydoc nvs_set_blob()
     */
    virtual esp_err_t hal_nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length) = 0;

    /**
     * @brief Read a blob from NVS.
     * @copydoc nvs_get_blob()
     */
    virtual esp_err_t hal_nvs_get_blob(nvs_handle_t handle, const char *key, void *out_value, size_t *length) = 0;

    /**
     * @brief Write any pending changes to non-volatile storage.
     * @copydoc nvs_commit()
     */
    virtual esp_err_t hal_nvs_commit(nvs_handle_t handle) = 0;
};
