#pragma once

#include "ultrasonic_sensor.hpp"
#include "driver/gpio.h"

class TrigerEcho : public UltrasonicSensor
{
public:
    TrigerEcho(gpio_num_t trig, const UltrasonicSensor::UltrasonicConfig &cfg);
    virtual ~TrigerEcho() = default;

    bool init() override;

protected:
    void send_ping();

    gpio_num_t trig_pin_;
};
