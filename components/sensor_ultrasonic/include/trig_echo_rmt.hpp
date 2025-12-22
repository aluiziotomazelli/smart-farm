#pragma once

#include "trig_echo.hpp"
#include "driver/gpio.h"
#include "driver/rmt_rx.h"

class TrigerEchoRmt : public TrigerEcho
{
public:
    TrigerEchoRmt(gpio_num_t                                trig,
                  gpio_num_t                                echo,
                  const UltrasonicSensor::UltrasonicConfig &cfg);

    bool init() override;

protected:
    bool read_raw_distance_cm(float &out_cm, UsFailure &out_failure) override;

private:
    gpio_num_t echo_pin_;

    rmt_channel_handle_t rx_channel_;
    rmt_symbol_word_t    rx_symbols_[64];
};
