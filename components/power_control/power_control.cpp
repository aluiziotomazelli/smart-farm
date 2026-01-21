#include "power_control.hpp"
#include "gpio_validator.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "esp_check.h"
#include "gpio_validator.hpp"
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
             config_.gpio, config_.inverted_logic ? "true" : "false",
             config_.initial_on ? "on" : "off");

    // Validate GPIO safety and mode
    ESP_RETURN_ON_ERROR(
        GpioValidator::validate(config_.gpio, GpioValidator::Mode::OUTPUT), TAG,
        "GPIO %d validation failed", config_.gpio);

    // // Check if GPIO is reserved for SPI flash
    // if (config_.gpio >= GPIO_NUM_6 && config_.gpio <= GPIO_NUM_11) {
    //     ESP_LOGE(TAG, "GPIO %d is reserved for SPI flash - CANNOT use as output",
    //              config_.gpio);
    //     ESP_LOGW(TAG, "Using flash pins (6-11) will corrupt flash operations and "
    //                   "cause resets");
    //     return ESP_ERR_INVALID_ARG;
    // }
    // // Check if GPIO is valid for output
    // if (!GPIO_IS_VALID_OUTPUT_GPIO(config_.gpio)) {
    //     ESP_LOGE(TAG, "Invalid GPIO %d", config_.gpio);
    //     return ESP_ERR_INVALID_ARG;
    // }

    // // Check if GPIO has special functions during boot
    // if (config_.gpio == GPIO_NUM_0 || config_.gpio == GPIO_NUM_2 ||
    //     config_.gpio == GPIO_NUM_12 || config_.gpio == GPIO_NUM_15) {
    //     ESP_LOGW(TAG, "GPIO %d has special functions during boot", config_.gpio);
    // }

    // Reset GPIO
    ESP_RETURN_ON_ERROR(gpio_reset_pin(config_.gpio), TAG, "Failed to reset GPIO %d",
                        config_.gpio);

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

    ESP_RETURN_ON_ERROR(gpio_set_drive_capability(config_.gpio, GPIO_DRIVE_CAP_3),
                        TAG, "Failed to set GPIO %d drive capability", config_.gpio);
    ESP_LOGD(TAG, "GPIO %d drive capability set to 40mA", config_.gpio);

    initialized_ = true;

    ESP_RETURN_ON_ERROR(config_.initial_on ? turnOn() : turnOff(), TAG,
                        "Failed to set initial state");

    ESP_LOGI(TAG, "Power control initialized successfully");
    return ESP_OK;
}

esp_err_t PowerControl::apply_gpio(bool enable)
{
    // Check if the power control is initialized
    if (!initialized_) {
        ESP_LOGE(TAG, "Power control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Set physical level to logic level
    bool level    = config_.inverted_logic ? !enable : enable;
    esp_err_t ret = gpio_set_level(config_.gpio, level); // Set GPIO

    if (ret == ESP_OK) {
        isOn_ = enable; // Update internal state
        ESP_LOGD(TAG, "GPIO %d enabled=%d (physical_level=%d)", config_.gpio, enable,
                 level);
        return ret;
    }

    ESP_LOGE(TAG, "Failed to set GPIO %d to enable=%d (physical_level=%d)",
             config_.gpio, enable, level);
    return ret;
}

esp_err_t PowerControl::turnOn()
{
    return apply_gpio(true);
}

esp_err_t PowerControl::turnOff()
{
    return apply_gpio(false);
}

esp_err_t PowerControl::toggle()
{
    return apply_gpio(!isOn_);
}

bool PowerControl::isOn() const
{
    return isOn_;
}

bool PowerControl::isInitialized() const
{
    return initialized_;
}

gpio_num_t PowerControl::getPin() const
{
    return config_.gpio;
}
esp_err_t PowerControl::deinit()
{
    if (!initialized_) {
        return ESP_OK;
    }

    // Forçar GPIO para LOW (estado seguro)
    ESP_RETURN_ON_ERROR(gpio_set_level(config_.gpio, 0), TAG,
                        "Failed to set GPIO low during deinit");

    // Resetar pino (volta para estado de alta impedância)
    esp_err_t ret = gpio_reset_pin(config_.gpio);
    if (ret == ESP_OK) {
        initialized_ = false;
        isOn_        = false; // Resetar estado interno também
        ESP_LOGI(TAG, "Power control deinitialized on GPIO %d", config_.gpio);
    }

    return ret;
}