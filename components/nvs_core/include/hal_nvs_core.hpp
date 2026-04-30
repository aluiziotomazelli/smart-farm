// include/hal_nvs.hpp
#pragma once

#include "esp_err.h"

#include "interfaces/i_hal_nvs.hpp"
#include "nvs_flash.h"

/**
 * @class HalNvs
 * @brief Hardware Abstraction Layer for NVS drivers (implementation)
 */
class HalNvs : public IHalNvs
{
public:
    HalNvs() = default;

    /** @copydoc IHalNvs::hal_nvs_flash_init() */
    esp_err_t hal_nvs_flash_init() override { return nvs_flash_init(); }

    /** @copydoc IHalNvs::hal_nvs_flash_erase() */
    esp_err_t hal_nvs_flash_erase() override { return nvs_flash_erase(); }

    /** @copydoc IHalNvs::hal_nvs_erase_all() */
    esp_err_t hal_nvs_erase_all(nvs_handle_t handle) override { return nvs_erase_all(handle); }

    /** @copydoc IHalNvs::hal_nvs_open() */
    esp_err_t hal_nvs_open(const char *name, nvs_open_mode_t open_mode, nvs_handle_t *out_handle) override
    {
        return nvs_open(name, open_mode, out_handle);
    }

    /** @copydoc IHalNvs::hal_nvs_close() */
    void hal_nvs_close(nvs_handle_t handle) override { nvs_close(handle); }

    /** @copydoc IHalNvs::hal_nvs_set_blob() */
    esp_err_t hal_nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length) override
    {
        return nvs_set_blob(handle, key, value, length);
    }

    /** @copydoc IHalNvs::hal_nvs_get_blob() */
    esp_err_t hal_nvs_get_blob(nvs_handle_t handle, const char *key, void *out_value, size_t *length) override
    {
        return nvs_get_blob(handle, key, out_value, length);
    }

    /** @copydoc IHalNvs::hal_nvs_commit() */
    esp_err_t hal_nvs_commit(nvs_handle_t handle) override { return nvs_commit(handle); }
};
