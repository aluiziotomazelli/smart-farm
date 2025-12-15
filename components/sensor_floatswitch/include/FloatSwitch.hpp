#pragma once

#include "driver/gpio.h"

class FloatSwitch
{
public:
    enum class WakeupEdge
    {
        FALLING, // Wake on HIGH to LOW transition
        RISING,  // Wake on LOW to HIGH transition
        ANY      // Wake on any edge (requires EXT1)
    };

    struct Config
    {
        gpio_num_t       pin;
        bool             normally_open = true; // true: NA, false: NF
        gpio_pull_mode_t pull_mode     = GPIO_PULLUP_ONLY;
        uint32_t         debounce_ms   = 50;              // Debounce time in milliseconds
        WakeupEdge       wakeup_edge   = WakeupEdge::ANY; // Edge type for wakeup
    };

    FloatSwitch(const Config &config);
    ~FloatSwitch();

    // Initialize GPIO/RTC_GPIO pin
    bool init();

    // Read current state (with debounce)
    bool read();

    // Get raw pin state (without debounce)
    bool read_raw();

    // Check if water level is high based on NO/NC configuration
    bool is_level_high();

    // Enable wakeup from deep sleep (configure RTC_GPIO)
    bool enable_wakeup();

    // Disable wakeup (release RTC_GPIO hold)
    bool disable_wakeup();

    // Check if pin is RTC capable
    bool is_rtc_capable() const { return rtc_capable; }

    // Get pin number
    gpio_num_t get_pin() const { return config.pin; }

    // Get configuration
    const Config &get_config() const { return config; }

private:
    Config   config;
    bool     initialized      = false;
    bool     rtc_capable      = false;
    bool     last_state       = false;
    uint64_t last_change_time = 0;

    // Debounce helper
    bool debounce_check(bool current_state);

    // RTC GPIO configuration helpers
    bool configure_rtc_gpio();
    void release_rtc_gpio();
};