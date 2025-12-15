#pragma once

#include "UltrasonicSensor.hpp"
#include "driver/gpio.h"

class HCSR04Gpio : public UltrasonicSensor
{
public:
    HCSR04Gpio(gpio_num_t trig, gpio_num_t echo, const UltrasonicSensor::UltrasonicConfig &cfg);

    bool init() override;

protected:
    float readRawDistanceCm() override;

private:
    gpio_num_t trig_pin;
    gpio_num_t echo_pin;
};