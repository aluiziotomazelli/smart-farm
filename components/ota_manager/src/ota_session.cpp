#include "ota_session.hpp"
#include "esp_https_ota.h"
#include "esp_log.h"

static const char* TAG = "OtaSession";

esp_err_t OtaSession::begin(const esp_http_client_config_t* config)
{
    if (ota_handle_ != nullptr) {
        ESP_LOGE(TAG, "OTA session already active");
        return ESP_ERR_INVALID_STATE;
    }

    esp_https_ota_config_t ota_config = {};
    ota_config.http_config = config;

    return esp_https_ota_begin(&ota_config, &ota_handle_);
}

esp_err_t OtaSession::get_img_desc(esp_app_desc_t* new_app_info)
{
    if (ota_handle_ == nullptr) {
        ESP_LOGE(TAG, "OTA session not active");
        return ESP_ERR_INVALID_STATE;
    }

    return esp_https_ota_get_img_desc(ota_handle_, new_app_info);
}

esp_err_t OtaSession::perform()
{
    if (ota_handle_ == nullptr) {
        ESP_LOGE(TAG, "OTA session not active");
        return ESP_ERR_INVALID_STATE;
    }

    return esp_https_ota_perform(ota_handle_);
}

bool OtaSession::is_complete() const
{
    if (ota_handle_ == nullptr) {
        return false;
    }

    return esp_https_ota_is_complete_data_received(ota_handle_) == ESP_OK;
}

esp_err_t OtaSession::finish()
{
    if (ota_handle_ == nullptr) {
        ESP_LOGE(TAG, "OTA session not active");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_https_ota_finish(ota_handle_);
    ota_handle_ = nullptr;
    return err;
}

esp_err_t OtaSession::abort()
{
    if (ota_handle_ == nullptr) {
        return ESP_OK;
    }

    esp_err_t err = esp_https_ota_abort(ota_handle_);
    ota_handle_ = nullptr;
    return err;
}

bool OtaSession::is_active() const
{
    return ota_handle_ != nullptr;
}
