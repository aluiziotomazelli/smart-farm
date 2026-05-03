#pragma once

#include "esp_err.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"

/**
 * @brief Interface for managing an OTA update session.
 */
class IOtaSession
{
public:
    virtual ~IOtaSession() = default;

    /** @copydoc esp_https_ota_begin() */
    virtual esp_err_t begin(const esp_http_client_config_t* config) = 0;

    /** @copydoc esp_https_ota_get_img_desc() */
    virtual esp_err_t get_img_desc(esp_app_desc_t* new_app_info) = 0;

    /** @copydoc esp_https_ota_perform() */
    virtual esp_err_t perform() = 0;

    /** @copydoc esp_https_ota_is_complete_data_received() */
    virtual bool is_complete() const = 0;

    /** @copydoc esp_https_ota_finish() */
    virtual esp_err_t finish() = 0;

    /** @copydoc esp_https_ota_abort() */
    virtual esp_err_t abort() = 0;

    /** @brief Checks if the OTA session is currently active. */
    virtual bool is_active() const = 0;
};
