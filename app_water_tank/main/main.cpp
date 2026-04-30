#include "water_tank_app.hpp"
#include "esp_log.h"
#include "protocol_types.hpp"
#include "float_switch.hpp"
#include "water_tank_storage_adapter.hpp"
#include "espnow_manager.hpp"
#include "power_control.hpp"
#include "hal_nvs_core.hpp"
#include "water_tank_nvs.hpp"
#include "ultrasonic_adapter.hpp"
#include "tank_geometry.hpp"
#include "hal_sleep.hpp"

#include "wifi_manager.hpp"

static const char* TAG = "main";

// Production Configuration
static constexpr gpio_num_t POWER_GPIO = GPIO_NUM_4;
static constexpr gpio_num_t US_TRIG_GPIO = GPIO_NUM_21;
static constexpr gpio_num_t US_ECHO_GPIO = GPIO_NUM_19;
static constexpr gpio_num_t FLOAT_SWITCH_GPIO = GPIO_NUM_18;

// Static allocation for production hardware components
static power_control::GpioHAL gpio_hal_pc;
static power_control::PowerControl power{gpio_hal_pc, POWER_GPIO, true, false};

static floatswitch::GpioHAL gpio_hal_fs;
static ultrasonic::UsSensor sensor_hw{
    US_TRIG_GPIO,
    US_ECHO_GPIO,
    {.ping_interval_ms = 70,
     .ping_duration_us = 20,
     .timeout_us = 25000,
     .filter = ultrasonic::Filter::DOMINANT_CLUSTER,
     .min_distance_cm = SENSOR_MIN_DISTANCE_CM,
     .max_distance_cm = SENSOR_MAX_DISTANCE_CM,
     .warmup_time_ms = 600}};

static floatswitch::TimerHAL timer_hal;
static floatswitch::FloatSwitch fs{
    {FLOAT_SWITCH_GPIO, true, 50000, floatswitch::ActiveLevel::LOW, floatswitch::WakeupCondition::WHEN_TANK_IS_EMPTY},
    gpio_hal_fs,
    timer_hal};

static SleepHAL sleep_hw;
static HalNvs hal_nvs;
static WaterTankNvs nvs{hal_nvs};

static UltrasonicLevelSensorAdapter sensor_adapter{sensor_hw};
static WaterTankStorageAdapter storage_adapter{nvs};
static TankGeometry geometry{SENSOR_OFFSET_CM};
static WaterTankLogic logic{geometry, fs};

extern "C" void app_main()
{
    ESP_LOGI(TAG, "Initializing Smart Farm Water Tank...");

    // Initialize hardware stack
    power.init();
    power.turn_on();

    sensor_hw.init();
    fs.init();
    nvs.init_partition();
    
    wifi_manager::WiFiManager& wifi = wifi_manager::WiFiManager::get_instance();
    wifi.init();
    wifi.start();

    espnow::EspNowConfig config;
    config.node_id = static_cast<espnow::NodeId>(FarmNodeId::WATER_TANK);
    config.node_type = static_cast<espnow::NodeType>(FarmNodeType::SENSOR);
    config.app_rx_queue = xQueueCreate(30, sizeof(espnow::AppMessage));
    config.wifi_channel = 1;

    espnow::EspNowManager& comm = espnow::EspNowManager::instance();
    comm.init(config);

    // Instantiate app with dependencies
    WaterTankApp app(sensor_adapter, fs, storage_adapter, comm, wifi, power, sleep_hw, logic);

    // Run the main application flow
    app.run();
}
