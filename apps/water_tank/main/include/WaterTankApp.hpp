#pragma once

#include "WaterTankTypes.hpp"

class WaterTankApp
{
public:
    void init();
    void run();

private:
    static FillState infer_fill_state(uint16_t current_level);
    static void      configure_sleep_policy(bool boia, uint64_t timer_us);
    static uint64_t  decide_timer_us(FillState state);
};