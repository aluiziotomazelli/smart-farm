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
        gpio_num_t  pin;
        bool        normally_open = true; // NO=true, NC=false
        Pull        pull          = Pull::UP;
        uint32_t    debounce_ms   = 50;
        WakeupLevel wakeup_edge   = WakeupLevel::LOW;
    };

    struct WakeupInfo
    {
        gpio_num_t pin;
        int        level; // 0 ou 1
    };

    explicit FloatSwitch(const Config &cfg);
    ~FloatSwitch();

    bool init();

    // Estado lógico: true = água presente
    bool read();

    // Estado elétrico do GPIO
    bool read_raw() const;

    // Wakeup como capability (não registra no sistema)
    bool get_wakeup_info(WakeupInfo &info) const;

    bool is_rtc_capable() const { return rtc_capable; }

private:
    Config config;

    bool rtc_capable = false;
    bool initialized = false;

    // debounce
    bool     stable_state       = false;
    bool     pending_state      = false;
    uint64_t last_transition_ms = 0;

    bool debounce_update(bool raw_state);

    bool configure_gpio();
    bool configure_rtc_gpio();
    void release_rtc_gpio();
};
