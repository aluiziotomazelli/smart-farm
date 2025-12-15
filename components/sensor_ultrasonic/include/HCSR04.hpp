#pragma once

#include "UltrasonicSensor.hpp"
#include "driver/gpio.h"

class HCSR04 : public UltrasonicSensor
{
public:
    HCSR04(gpio_num_t trig, const UltrasonicSensor::UltrasonicConfig &cfg);
    virtual ~HCSR04() = default;

    bool init() override;

protected:
    void send_ping();

protected:
    gpio_num_t trig_pin;
};
