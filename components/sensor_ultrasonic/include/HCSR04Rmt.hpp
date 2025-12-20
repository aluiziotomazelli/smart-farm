#pragma once

#include "HCSR04.hpp"
#include "driver/gpio.h"
#include "driver/rmt_rx.h"

class HCSR04Rmt : public HCSR04
{
public:
    HCSR04Rmt(gpio_num_t trig, gpio_num_t echo, const UltrasonicSensor::UltrasonicConfig &cfg);

    bool init() override;

protected:
    bool readRawDistanceCm(float &out_cm, UsFailure &out_failure) override;

private:
    gpio_num_t echo_pin;

    rmt_channel_handle_t rx_channel;
    rmt_symbol_word_t    rx_symbols[64];
};
