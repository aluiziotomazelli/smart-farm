#pragma once

#include "esp_sleep.h"
#include "esp_err.h"

/**
 * @class ISleepHAL
 * @brief Interface for ESP32 sleep functionality.
 *
 * This HAL abstracts the ESP-IDF sleep APIs to allow for host-based testing
 * and to prevent direct dependency on hardware-specific headers in business logic.
 */
class ISleepHAL {
public:
    virtual ~ISleepHAL() = default;

    /** @copydoc esp_sleep_disable_wakeup_source() */
    virtual esp_err_t disable_wakeup_source(esp_sleep_source_t source) = 0;

    /** @copydoc esp_sleep_enable_timer_wakeup() */
    virtual esp_err_t enable_timer_wakeup(uint64_t time_in_us) = 0;

    /** @copydoc esp_deep_sleep_start() */
    virtual void deep_sleep_start() = 0;
};
