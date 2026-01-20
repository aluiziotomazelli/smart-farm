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
        bool inverted_logic = false; // true: LOW liga, false: HIGH liga
        bool initial_on     = false; // estado no init
    };

    PowerControl(const PowerControl::Config &cfg);

    ~PowerControl() = default;

    esp_err_t init();
    esp_err_t deinit();
    esp_err_t turnOn();
    esp_err_t turnOff();
    esp_err_t toggle();
    bool isOn() const;
    bool isInitialized() const;
    gpio_num_t getPin() const;

private:
    esp_err_t apply_gpio(bool enable);
    Config config_;
    bool initialized_ = false;
    bool isOn_        = false;
};
