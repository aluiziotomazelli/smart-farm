#include "PowerControl.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

PowerControl::PowerControl(const PowerControl::Config &cfg)
    : config_(cfg)
{
}

esp_err_t PowerControl::init()
{
    gpio_config_t io_conf = {};
    io_conf.mode          = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask  = 1ULL << config_.enable_gpio;
    io_conf.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en    = GPIO_PULLUP_DISABLE;
    io_conf.intr_type     = GPIO_INTR_DISABLE;

    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK)
    {
        return err;
    }

    if (config_.initial_on)
    {
        return on();
    }
    else
    {
        return off();
    }
}

esp_err_t PowerControl::on()
{
    apply_gpio(true);
    state_ = true;
    return ESP_OK;
}

esp_err_t PowerControl::off()
{
    apply_gpio(false);
    state_ = false;
    return ESP_OK;
}

bool PowerControl::is_on() const { return state_; }

void PowerControl::apply_gpio(bool enable)
{
    bool level = config_.active_high ? enable : !enable;
    gpio_set_level(config_.enable_gpio, level);
}
