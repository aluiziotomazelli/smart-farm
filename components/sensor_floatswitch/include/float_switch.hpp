#pragma once

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

    enum class Pull
    {
        UP,
        DOWN,
        NONE
    };

    struct Config
    {
        gpio_num_t gpio;
        bool normally_open       = true; // NO=true, NC=false
        Pull pull                = Pull::UP;
        uint32_t debounce_ms     = 50;
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

    bool init();

    // Estado lógico: true = água presente
    bool read();

    // Estado elétrico do GPIO
    bool read_raw() const;

    // Informação para configurar wakeup (não registra no sistema)
    bool get_wakeup_info(WakeupInfo &info) const;

private:
    Config config;
    bool initialized = false;

    // debounce
    bool stable_state           = false;
    bool pending_state          = false;
    uint64_t last_transition_ms = 0;

    bool debounce_update(bool raw_state);
    bool configure_gpio();
};