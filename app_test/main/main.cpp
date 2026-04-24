#include "wifi_manager.hpp"
#include "float_switch.hpp"
#include "power_control.hpp"
#include "us_sensor.hpp"
#include <cstdint>
#include "soc/gpio_num.h"

extern "C" void app_main(void)
{
    // Instantiate WifiManager
    auto& wifi = wifi_manager::WiFiManager::get_instance(); // Singleton instance
    wifi.init();

    // Instantiate FloatSwitch
    static floatswitch::GpioHAL gpio;
    static floatswitch::TimerHAL timer;

    floatswitch::Config cfg = {
        .gpio = GPIO_NUM_4,
        .normally_open = true,
        .debounce_time_us = 50000,
        .active_level = floatswitch::ActiveLevel::LOW,
        .wakeup_on = floatswitch::WakeupCondition::WHEN_TANK_IS_EMPTY};

    floatswitch::FloatSwitch sensor_float(cfg, gpio, timer);
    sensor_float.init();

    // Instantiate PowerControl
    power_control::GpioHAL gpio_hal;
    power_control::PowerControl power_control(gpio_hal, GPIO_NUM_4);
    power_control.init();

    // Instantiate Ultrasonic Sensor
    const gpio_num_t TRIG_PIN = GPIO_NUM_21;
    const gpio_num_t ECHO_PIN = GPIO_NUM_7;

    ultrasonic::UsConfig us_cfg = {
        .ping_interval_ms = 70,
        .ping_duration_us = 20,
        .timeout_us = 25000,
        .filter = ultrasonic::Filter::DOMINANT_CLUSTER,
        .min_distance_cm = 25.0f,
        .max_distance_cm = 200.0f,
        .warmup_time_ms = 0};

    ultrasonic::UsSensor us_sensor(TRIG_PIN, ECHO_PIN, us_cfg);
    us_sensor.init();
}