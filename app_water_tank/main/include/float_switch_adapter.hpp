#pragma once
#include "interfaces/i_float_switch.hpp"
#include "float_switch.hpp"

/**
 * @class FloatSwitchAdapter
 * @brief Adapter for the FloatSwitch component to match IFloatSwitch interface.
 */
class FloatSwitchAdapter : public IFloatSwitch
{
public:
    FloatSwitchAdapter(FloatSwitch &fs) : fs_(fs) {}

    bool is_active() const override
    {
        return fs_.isTankFull(); // Map isTankFull to the generic is_active
    }

    bool should_enable_wakeup() const override
    {
        return fs_.shouldEnableWakeup();
    }

private:
    FloatSwitch &fs_;
};
