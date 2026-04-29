#include "water_tank_app.hpp"
#include "esp_log.h"
#include "esp_sleep.h"
#include "app_protocol_types.hpp"

// Production hardware includes
#include "protocol_types.hpp"
#include "ultrasonic_adapter.hpp"
#include "float_switch.hpp"
#include "water_tank_storage_adapter.hpp"
#include "tank_geometry.hpp"
#include "water_tank_logic.hpp"
#include "espnow_manager.hpp"
#include "power_control.hpp"
#include "hal_nvs.hpp"
#include "water_tank_nvs.hpp"

static const char* TAG = "WaterTankApp";

// Production Configuration
static constexpr gpio_num_t POWER_GPIO = GPIO_NUM_4;
static constexpr gpio_num_t US_TRIG_GPIO = GPIO_NUM_21;
static constexpr gpio_num_t US_ECHO_GPIO = GPIO_NUM_19;
static constexpr gpio_num_t FLOAT_SWITCH_GPIO = GPIO_NUM_18;

// Static allocation for production stack
struct ProductionStack
{
    power_control::GpioHAL gpio_hal_pc;
    power_control::PowerControl power{gpio_hal_pc, POWER_GPIO, true, false};

    floatswitch::GpioHAL gpio_hal_fs;
    ultrasonic::UsSensor sensor_hw{
        US_TRIG_GPIO,
        US_ECHO_GPIO,
        {.ping_interval_ms = 70,
         .ping_duration_us = 20,
         .timeout_us = 25000,
         .filter = ultrasonic::Filter::DOMINANT_CLUSTER,
         .min_distance_cm = SENSOR_MIN_DISTANCE_CM,
         .max_distance_cm = SENSOR_MAX_DISTANCE_CM,
         .warmup_time_ms = 0}};

    floatswitch::TimerHAL timer_hal;
    floatswitch::FloatSwitch fs{
        {FLOAT_SWITCH_GPIO,
         true,
         50000,
         floatswitch::ActiveLevel::LOW,
         floatswitch::WakeupCondition::WHEN_TANK_IS_EMPTY},
        gpio_hal_fs,
        timer_hal};

    HalNvs hal_nvs;
    WaterTankNvs nvs{hal_nvs};

    UltrasonicLevelSensorAdapter sensor_adapter{sensor_hw};
    WaterTankStorageAdapter storage_adapter{nvs};
    TankGeometry geometry{TANK_HEIGHT_CM, SENSOR_OFFSET_CM};
    WaterTankLogic logic{geometry, fs};
};

static ProductionStack s_prod_stack;

WaterTankApp::WaterTankApp() {}

WaterTankApp::WaterTankApp(
    ILevelSensor& sensor,
    floatswitch::IFloatSwitch& float_switch,
    IWaterTankStorage& storage,
    espnow::IEspNowManager& comm,
    WaterTankLogic& logic)
    : sensor_(&sensor)
    , float_switch_(&float_switch)
    , storage_(&storage)
    , comm_(&comm)
    , logic_(&logic)
{
}

void WaterTankApp::init()
{
    ESP_LOGI(TAG, "Initializing hardware stack...");

    s_prod_stack.power.init();
    s_prod_stack.power.turn_on();

    s_prod_stack.sensor_hw.init();
    s_prod_stack.fs.init();
    s_prod_stack.nvs.init_partition();

    espnow::EspNowConfig config;
    config.node_id = static_cast<espnow::NodeId>(FarmNodeId::WATER_TANK);
    config.node_type = static_cast<espnow::NodeType>(FarmNodeType::SENSOR);
    config.app_rx_queue = xQueueCreate(30, sizeof(espnow::AppMessage));
    config.wifi_channel = 1;

    espnow::EspNowManager& comm = espnow::EspNowManager::instance();
    comm.init(config);

    // Link pointers
    sensor_ = &s_prod_stack.sensor_adapter;
    float_switch_ = &s_prod_stack.fs;
    storage_ = &s_prod_stack.storage_adapter;
    comm_ = &comm;
    logic_ = &s_prod_stack.logic;
}

void WaterTankApp::run()
{
    ESP_LOGI(TAG, "Starting application flow");

    // 1. Load state and statistics from persistent storage
    if (storage_->load(stats_) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load storage, using defaults");
        storage_->reset_to_defaults(stats_);
    }

    // 2. Perform sensor reading
    ultrasonic::Reading reading = sensor_->read_level();
    ESP_LOGI(TAG, "Reading raw: %.1f cm (Status: %d)", reading.cm, static_cast<int>(reading.result));

    // 3. Process logic (Brain)
    logic_->process_reading(reading, stats_);
    logic_->update_operation_mode(stats_);

    ESP_LOGI(
        TAG,
        "Result: %d, Distance: %.1f cm, Level: %d permille, Mode: %s",
        static_cast<int>(stats_.last_result),
        stats_.last_distance_cm,
        stats_.level_permille,
        stats_.backup_mode_active ? "BACKUP" : "NORMAL");

    // 4. Save updated state
    storage_->save(stats_);

    // 5. Transmit data to Hub
    send_report();

    // 6. Calculate sleep and enter deep sleep
    uint64_t sleep_time_us = logic_->calculate_sleep_time_us(stats_);
    enter_deep_sleep(sleep_time_us);
}

// =====================================================================
// PRIVATE METHODS
// =====================================================================

esp_err_t WaterTankApp::send_report()
{
    WaterLevelReport report = {};

    report.level_permille = stats_.level_permille;
    report.distance_cm = stats_.last_distance_cm;
    report.battery_mv = 0;
    report.status = map_status(stats_.last_result);

    report.float_switch_is_full = float_switch_->is_tank_full();
    report.backup_mode_active = stats_.backup_mode_active;

    ESP_LOGI(TAG, "Sending report: %d permille", report.level_permille);

    esp_err_t err = comm_->send_data(
        espnow::ReservedIds::HUB,
        static_cast<uint8_t>(FarmPayloadType::WATER_LEVEL_REPORT),
        &report,
        sizeof(report),
        true // require_ack
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send report: %s", esp_err_to_name(err));
    }
    return err;
}

SensorStatus WaterTankApp::map_status(ultrasonic::UsResult result)
{
    switch (result) {
    case ultrasonic::UsResult::OK:
        return SensorStatus::OK;
    case ultrasonic::UsResult::WEAK_SIGNAL:
        return SensorStatus::WARNING_LOW_SIGNAL;
    case ultrasonic::UsResult::TIMEOUT:
        return SensorStatus::ERROR_TIMEOUT;
    case ultrasonic::UsResult::OUT_OF_RANGE:
        return SensorStatus::ERROR_OUT_OF_RANGE;
    case ultrasonic::UsResult::HIGH_VARIANCE:
    case ultrasonic::UsResult::INSUFFICIENT_SAMPLES:
        return SensorStatus::ERROR_UNSTABLE;
    case ultrasonic::UsResult::ECHO_STUCK:
    case ultrasonic::UsResult::HW_FAULT:
        return SensorStatus::ERROR_HARDWARE;
    default:
        return SensorStatus::UNKNOWN;
    }
}

void WaterTankApp::enter_deep_sleep(uint64_t sleep_time_us)
{
    ESP_LOGI(TAG, "Entering deep sleep for %llu us", sleep_time_us);

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

    if (sleep_time_us > 0) {
        esp_sleep_enable_timer_wakeup(sleep_time_us);
    }

    esp_deep_sleep_start();
}
