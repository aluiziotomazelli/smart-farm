#include "wifi_manager.hpp"
#include "float_switch.hpp"
#include "power_control.hpp"
#include "us_sensor.hpp"
#include <cstdint>
#include "soc/gpio_num.h"
#include "espnow_manager.hpp"

extern "C" void app_main(void)
{
    // =====================================
    // WifiManager
    // =====================================
    auto& wifi = wifi_manager::WiFiManager::get_instance(); // Singleton instance
    wifi.init();

    // =====================================
    // FloatSwitch
    // =====================================
    // HAL implementations for DI FloatSwitch
    static floatswitch::GpioHAL gpio;
    static floatswitch::TimerHAL timer;

    // Configuration for FloatSwitch
    floatswitch::Config cfg = {
        .gpio = GPIO_NUM_4,
        .normally_open = true,
        .debounce_time_us = 50000,
        .active_level = floatswitch::ActiveLevel::LOW,
        .wakeup_on = floatswitch::WakeupCondition::WHEN_TANK_IS_EMPTY};

    // Instantiate FloatSwitch
    floatswitch::FloatSwitch sensor_float(cfg, gpio, timer);
    sensor_float.init();

    // =====================================
    // PowerControl
    // =====================================
    power_control::GpioHAL gpio_hal;
    power_control::PowerControl power_control(gpio_hal, GPIO_NUM_4);
    power_control.init();

    // =====================================
    // Ultrasonic Sensor
    // =====================================
    // Pins for Ultrasonic Sensor
    const gpio_num_t TRIG_PIN = GPIO_NUM_21;
    const gpio_num_t ECHO_PIN = GPIO_NUM_7;

    // Configuration for Ultrasonic Sensor
    ultrasonic::UsConfig us_cfg = {
        .ping_interval_ms = 70,
        .ping_duration_us = 20,
        .timeout_us = 25000,
        .filter = ultrasonic::Filter::DOMINANT_CLUSTER,
        .min_distance_cm = 25.0f,
        .max_distance_cm = 200.0f,
        .warmup_time_ms = 0};

    // Instantiate Ultrasonic Sensor
    ultrasonic::UsSensor us_sensor(TRIG_PIN, ECHO_PIN, us_cfg);
    us_sensor.init();

    // =====================================
    // EspNowManager
    // =====================================
    // Queue to receive messages from EspNowManager
    QueueHandle_t app_queue = xQueueCreate(30, sizeof(AppMessage));

    // Configuration for EspNowManager
    EspNowConfig config;
    config.node_id = ReservedIds::HUB;
    config.node_type = ReservedTypes::HUB;
    config.app_rx_queue = app_queue;
    config.wifi_channel = 1;

    // Initialize EspNowManager
    EspNowManager& manager = EspNowManager::instance();
    manager.init(config);
}