#pragma once

/**
 * @class IFloatSwitch
 * @brief Interface for a physical float switch.
 *
 * Provides status of the backup float switch, abstracting GPIO reads.
 */
class IFloatSwitch
{
public:
    virtual ~IFloatSwitch() = default;

    /**
     * @brief Checks if the float switch is currently active (e.g. tank full/empty depending on config).
     * @return true if the switch is active.
     */
    virtual bool is_active() const = 0;

    /**
     * @brief Determines if the float switch should trigger a system wakeup.
     * @return true if the wakeup condition is met.
     */
    virtual bool should_enable_wakeup() const = 0;
};
