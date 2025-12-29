#include "power_control.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "PowerControl";

PowerControl::PowerControl(const PowerControl::Config &cfg)
    : config_(cfg)
{
}

esp_err_t PowerControl::init()
{
    ESP_LOGI(TAG, "Initializing power control on GPIO %d (active_%s, initial_%s)",
             config_.gpio, config_.inverted_logic ? "high" : "low",
             config_.initial_on ? "on" : "off");

    // Set GPIO as output
    gpio_config_t io_conf = {};
    io_conf.mode          = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask  = 1ULL << config_.gpio;
    io_conf.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en    = GPIO_PULLUP_DISABLE;
    io_conf.intr_type     = GPIO_INTR_DISABLE;

    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "Failed to configure GPIO %d",
                        config_.gpio);
    ESP_LOGD(TAG, "GPIO %d configured successfully", config_.gpio);

    ESP_RETURN_ON_ERROR(gpio_set_drive_capability(config_.gpio, GPIO_DRIVE_CAP_3), TAG,
                        "Failed to set GPIO %d drive capability", config_.gpio);
    ESP_LOGD(TAG, "GPIO %d drive capability set to 40mA", config_.gpio);

    ESP_RETURN_ON_ERROR(config_.initial_on ? on() : off(), TAG,
                        "Failed to set initial state");

    ESP_LOGI(TAG, "Power control initialized successfully");
    return ESP_OK;
}

esp_err_t PowerControl::on()
{
    esp_err_t ret = apply_gpio(true);
    if (ret != ESP_OK) {
        return ret;
    }
    state_ = true;
    ESP_LOGD(TAG, "Power ON successful");
    return ESP_OK;
}

esp_err_t PowerControl::off()
{
    esp_err_t ret = apply_gpio(false);
    if (ret != ESP_OK) {
        return ret;
    }
    state_ = false;
    ESP_LOGD(TAG, "Power OFF successful");
    return ESP_OK;
}

bool PowerControl::is_on() const
{
    return state_;
}

esp_err_t PowerControl::apply_gpio(bool enable)
{
    bool level = config_.inverted_logic ? !enable : enable;
    ESP_RETURN_ON_ERROR(gpio_set_level(config_.gpio, level), TAG,
                        "GPIO: %d set level: %d failed", config_.gpio, level);

    ESP_LOGD(TAG, "GPIO %d set to %d", config_.gpio, level);
    return ESP_OK;
}
