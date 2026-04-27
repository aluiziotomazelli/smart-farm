#include "water_tank_app.hpp"
#include "ultrasonic_adapter.hpp"
#include "float_switch.hpp"
#include "water_tank_storage_adapter.hpp"
#include "tank_geometry.hpp"
#include "water_tank_logic.hpp"
#include "espnow_manager.hpp"
#include "power_control.hpp"
#include "hal_nvs.hpp" // Adicionado para HalNvs

#include "esp_log.h"

static const char* TAG = "main";

static constexpr gpio_num_t POWER_GPIO = GPIO_NUM_4;
static constexpr gpio_num_t US_TRIG_GPIO = GPIO_NUM_21;
static constexpr gpio_num_t US_ECHO_GPIO = GPIO_NUM_19;
static constexpr gpio_num_t FLOAT_SWITCH_GPIO = GPIO_NUM_18;

extern "C" void app_main()
{
    ESP_LOGI(TAG, "Initializing Smart Farm Water Tank...");

    // 1. Hardware Initialization (Power Control)
    static power_control::GpioHAL gpio_hal_pc;
    power_control::PowerControl power(gpio_hal_pc, POWER_GPIO, /*inverted_logic*/ true, /*initial_on*/ false);
    power.init();
    power.turn_on();

    // 2. Physical Drivers
    // Ultrasonic Sensor
    static floatswitch::GpioHAL gpio_hal_fs;
    ultrasonic::UsConfig us_cfg = {
        .ping_interval_ms = 70,
        .ping_duration_us = 20,
        .timeout_us = 25000,
        .filter = ultrasonic::Filter::DOMINANT_CLUSTER,
        .min_distance_cm = 25.0f,
        .max_distance_cm = 200.0f,
        .warmup_time_ms = 0};

    ultrasonic::UsSensor sensor(US_TRIG_GPIO, US_ECHO_GPIO, us_cfg);
    sensor.init();

    // Float Switch
    static floatswitch::TimerHAL timer_hal;
    floatswitch::Config cfg = {
        .gpio = FLOAT_SWITCH_GPIO,                     // GPIO pin number
        .normally_open = true,                         // true = Normally Open, false = Normally Closed
        .debounce_time_us = 50000,                     // Debounce time in microseconds
        .active_level = floatswitch::ActiveLevel::LOW, // LOW = Active when low, HIGH = Active when high
        .wakeup_on = floatswitch::WakeupCondition::WHEN_TANK_IS_EMPTY}; // NEVER, WHEN_TANK_IS_EMPTY, WHEN_TANK_IS_FULL

    floatswitch::FloatSwitch fs(cfg, gpio_hal_fs, timer_hal); // GPIO, ActiveLevel
    fs.init();

    // NVS Storage
    HalNvs hal;            // HAL para NVS
    WaterTankNvs nvs(hal); // Injeção do HAL
    nvs.init_partition();

    // ESP-NOW Communication
    QueueHandle_t app_queue = xQueueCreate(30, sizeof(espnow::AppMessage));

    espnow::EspNowConfig config;
    config.node_id = static_cast<espnow::NodeId>(FarmNodeId::WATER_TANK);
    config.node_type = static_cast<espnow::NodeType>(FarmNodeType::SENSOR);
    config.app_rx_queue = app_queue;
    config.wifi_channel = 1;

    espnow::EspNowManager& comm = espnow::EspNowManager::instance();
    comm.init(config);

    // 3. Adapters (Interfaces)
    UltrasonicLevelSensorAdapter sensor_adapter(sensor);
    WaterTankStorageAdapter storage_adapter(nvs);

    // 4. Logic & Geometry
    TankGeometry geometry(LEVEL_MIN_CM, LEVEL_MAX_CM);
    WaterTankLogic logic(geometry, fs);

    // 5. Orchestrator
    WaterTankApp app(sensor_adapter, fs, storage_adapter, comm, logic);

    // 6. Run Application
    app.run();
}
