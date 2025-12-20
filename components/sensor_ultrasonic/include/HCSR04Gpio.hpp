#pragma once

#include "HCSR04.hpp"
#include "driver/gpio.h"

class HCSR04Gpio : public HCSR04
{
public:
    HCSR04Gpio(gpio_num_t trig, gpio_num_t echo, const UltrasonicSensor::UltrasonicConfig &cfg);

    bool init() override;

protected:
    bool readRawDistanceCm(float &out_cm, UsFailure &out_failure) override;

private:
    gpio_num_t echo_pin;
};
