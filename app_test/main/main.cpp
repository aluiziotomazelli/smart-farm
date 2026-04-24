#include "wifi_manager.hpp"
#include "float_switch.hpp"
#include "power_control.hpp"

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
}
