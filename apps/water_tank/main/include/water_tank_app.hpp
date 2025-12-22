#pragma once

#include "comm_interface.hpp"
#include "power_control.hpp"
#include "water_tank_nvs.hpp"
#include "water_tank_types.hpp"

class WaterTankApp
{
public:
    WaterTankApp();

    void init();
    void run();

private:
    static FillState infer_fill_state(uint16_t current_level);
    static void      configure_sleep_policy(bool boia, uint64_t timer_us);

    static uint64_t      decide_timer_us(FillState state);
    PowerControl         sensor_power_;
    WaterTankNvs         storage_;
    comm::CommInterface &comm_;
};