#pragma once

#include "interfaces/i_hal_sleep.hpp"
#include "esp_sleep.h"

/**
 * @class SleepHAL
 * @brief Concrete implementation of ISleepHAL wrapping ESP-IDF sleep APIs.
 * This implementation is inlined as it is a thin wrapper.
 */
class SleepHAL : public ISleepHAL
{
public:
    /** @copydoc ISleepHAL::disable_wakeup_source() */
    inline esp_err_t disable_wakeup_source(esp_sleep_source_t source) override
    {
        return esp_sleep_disable_wakeup_source(source);
    }

    /** @copydoc ISleepHAL::enable_timer_wakeup() */
    inline esp_err_t enable_timer_wakeup(uint64_t time_in_us) override
    {
        return esp_sleep_enable_timer_wakeup(time_in_us);
    }

    /** @copydoc ISleepHAL::deep_sleep_start() */
    inline void deep_sleep_start() override { esp_deep_sleep_start(); }
};
