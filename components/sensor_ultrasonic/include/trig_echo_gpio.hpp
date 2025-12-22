#pragma once

#include "trig_echo.hpp"
#include "driver/gpio.h"

class TrigerEchoGpio : public TrigerEcho
{
public:
    TrigerEchoGpio(gpio_num_t                                trig,
               gpio_num_t                                echo,
               const UltrasonicSensor::UltrasonicConfig &cfg);

    bool init() override;

protected:
    bool read_raw_distance_cm(float &out_cm, UsFailure &out_failure) override;

private:
    gpio_num_t echo_pin_;
};
