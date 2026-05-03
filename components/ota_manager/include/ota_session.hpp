#pragma once

#include "interfaces/i_ota_session.hpp"

/**
 * @brief Concrete implementation of IOtaSession.
 */
class OtaSession : public IOtaSession
{
public:
    /** @copydoc IOtaSession::begin */
    esp_err_t begin(const esp_http_client_config_t* config) override;

    /** @copydoc IOtaSession::get_img_desc */
    esp_err_t get_img_desc(esp_app_desc_t* new_app_info) override;

    /** @copydoc IOtaSession::perform */
    esp_err_t perform() override;

    /** @copydoc IOtaSession::is_complete */
    bool is_complete() const override;

    /** @copydoc IOtaSession::finish */
    esp_err_t finish() override;

    /** @copydoc IOtaSession::abort */
    esp_err_t abort() override;

    /** @brief Checks if the OTA session is currently active. */
    bool is_active() const;

private:
    /** @brief The handle to the esp_https_ota session. */
    esp_https_ota_handle_t ota_handle_ = nullptr;
};
