#pragma once

#include "esp_err.h"
#include <cstdint>
#include "driver/gpio.h"

class FloatSwitch
{
public:
    enum class WakeupLevel
    {
        LOW,
        HIGH,
    };

    enum class ActiveLevel
    {
        LOW,
        HIGH,
    };

    struct Config
    {
        gpio_num_t gpio;
        bool normally_open       = true; // NO=true, NC=false
        ActiveLevel active_level = ActiveLevel::LOW;
        WakeupLevel wakeup_level = WakeupLevel::LOW;
    };

    struct WakeupInfo
    {
        uint64_t gpio_mask;
        uint8_t mode;
    };

    // int level; // 0 ou 1
    // gpio_num_t gpio;
    explicit FloatSwitch(const Config &cfg);
    ~FloatSwitch() = default;

    esp_err_t init();

    // Electrical contact is closed
    bool isContactClosed() const;

    // Estado lógico: true = água presente
    bool isTankFull();

    // Informação para configurar wakeup (não registra no sistema)
    bool getWakeupInfo(WakeupInfo &info) const;

private:
    Config cfg_;
    esp_err_t initialized_ = ESP_FAIL;
    esp_err_t configureGpio();
};