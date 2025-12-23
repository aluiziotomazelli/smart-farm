#pragma once

#include "power_control.hpp"
#include "water_tank_nvs.hpp"
#include "comm_espnow.hpp"

class WaterTankApp
{
public:
    WaterTankApp();

    void init();
    void run();

private:
    FillState infer_fill_state(uint16_t current_level);
    uint64_t  decide_timer_us(FillState state);
    void      configure_sleep_policy(bool float_switch_closed, uint64_t timer_us);
    void      on_comm_receive(uint32_t source_node_id, const uint8_t* payload, size_t len);

private:
    PowerControl   sensor_power_;
    WaterTankNvs   storage_;
    comm::CommEspNow& comm_;
};
