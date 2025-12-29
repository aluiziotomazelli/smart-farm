/**
 * @file float_switch.hpp
 * @brief Defines the FloatSwitch class for reading the state of a float switch sensor.
 *
 * This component provides an abstraction for a float switch, handling GPIO configuration,
 * debouncing, and logical interpretation of the switch's state (contact closed/open)
 * to a physical state (tank full/empty). It also includes logic to determine
 * whether the ESP32 should be configured to wake up from deep sleep based on the
 * switch's state, preventing wake-up loops.
 */
#pragma once

#include "esp_err.h"
#include <cstdint>
#include "driver/gpio.h"

class FloatSwitch
{
public:
    /**
     * @brief Defines the logical condition under which a GPIO wake-up should be triggered.
     * This abstracts the electrical signal (LOW/HIGH) into a desired application behavior.
     */
    enum class WakeupCondition
    {
        NEVER,            ///< Never trigger a wake-up.
        WHEN_TANK_IS_EMPTY, ///< Trigger wake-up when the tank becomes empty.
        WHEN_TANK_IS_FULL,  ///< Trigger wake-up when the tank becomes full.
    };

    /**
     * @brief Defines the electrical level that represents an "active" state.
     * This is determined by the hardware wiring (e.g., pull-up vs. pull-down resistor).
     */
    enum class ActiveLevel
    {
        LOW,  ///< The signal is LOW when the switch contact is closed.
        HIGH, ///< The signal is HIGH when the switch contact is closed.
    };

    /**
     * @brief Configuration structure for the FloatSwitch.
     */
    struct Config
    {
        gpio_num_t gpio;                   ///< The GPIO pin the switch is connected to.
        bool normally_open = true;         ///< Switch type: true for Normally Open (NO), false for Normally Closed (NC).
        ActiveLevel active_level = ActiveLevel::LOW; ///< Electrical level when the contact is closed.
        WakeupCondition wakeup_on = WakeupCondition::NEVER; ///< Logical condition for triggering a GPIO wake-up.
    };

    /**
     * @brief Constructs a new FloatSwitch object.
     * @param cfg The configuration for the float switch.
     */
    explicit FloatSwitch(const Config &cfg);
    ~FloatSwitch() = default;

    /**
     * @brief Initializes the float switch.
     * Configures the GPIO pin with the appropriate pull-up or pull-down resistor.
     * @return ESP_OK on success, or an error code on failure.
     */
    esp_err_t init();

    /**
     * @brief Checks if the electrical contact of the switch is closed.
     * This method performs debouncing by taking multiple samples.
     * @note This is a raw electrical reading and does not interpret the physical meaning.
     * @return True if the contact is determined to be closed, false otherwise.
     */
    bool isContactClosed() const;

    /**
     * @brief Determines the logical state of the tank (full or empty).
     * This method interprets the raw electrical state (`isContactClosed`) based on the
     * `normally_open` configuration to determine the physical state.
     * @return True if the tank is considered full, false if it is empty.
     */
    bool isTankFull();

    /**
     * @brief Determines if the GPIO wake-up source should be enabled for the next sleep cycle.
     *
     * This function contains the critical logic to prevent wake-up loops. It checks the current
     * state of the tank against the desired `WakeupCondition`. A wake-up is only recommended
     * if the tank is NOT currently in the state that would cause an immediate wake-up.
     *
     * For example, if `wakeup_on` is `WHEN_TANK_IS_EMPTY`, this function will only return `true`
     * if the tank is currently FULL, thereby arming the trigger for when it becomes empty.
     *
     * @return True if the GPIO wake-up should be armed, false otherwise.
     */
    bool shouldEnableWakeup() const;

private:
    Config cfg_;
    esp_err_t initialized_ = ESP_FAIL;

    /**
     * @brief Configures the GPIO pin based on the `active_level`.
     * @return ESP_OK on success.
     */
    esp_err_t configureGpio();
};