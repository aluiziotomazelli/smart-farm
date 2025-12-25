#pragma once

#include "esp_err.h"
#include <cstdint>
#include "driver/gpio.h"

class PowerControl
{
public:
    struct Config
    {
        gpio_num_t gpio;
        bool active_high = true;  // true: HIGH liga, false: LOW liga
        bool initial_on  = false; // estado no init
    };

    PowerControl(const PowerControl::Config &cfg);

    esp_err_t init();

    esp_err_t on();
    esp_err_t off();

    bool is_on() const;

private:
    Config config_;
    bool state_ = false;

    void apply_gpio(bool enable);
};
