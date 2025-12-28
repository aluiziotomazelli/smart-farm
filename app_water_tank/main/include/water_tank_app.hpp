#pragma once

#include "espnow_comm.hpp"
#include "power_control.hpp"
#include "water_tank_nvs.hpp"

class WaterTankApp
{
public:
    WaterTankApp();

    void init();
    void run();

private:
    FillState infer_fill_state(uint16_t current_level);
    uint64_t decide_timer_us(FillState state);
    void configure_sleep_policy(bool float_switch_closed, uint64_t timer_us);

    void on_espnow_receive(uint8_t node_id, const uint8_t *data, int len, int8_t rssi);
    void on_espnow_send(uint8_t node_id, esp_now_send_status_t status);

private:
    PowerControl sensor_power_;
    WaterTankNvs storage_;
    EspNowComm comm_;
};
