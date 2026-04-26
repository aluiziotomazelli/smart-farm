#include "water_tank_app.hpp"
#include "esp_log.h"
#include "esp_sleep.h"
#include "app_protocol_types.hpp"

static const char *TAG = "WaterTankApp";

WaterTankApp::WaterTankApp(ILevelSensor &sensor, 
                           floatswitch::IFloatSwitch &float_switch, 
                           IWaterTankStorage &storage,
                           IEspNowManager &comm,
                           WaterTankLogic &logic)
    : sensor_(sensor), 
      float_switch_(float_switch), 
      storage_(storage), 
      comm_(comm), 
      logic_(logic)
{
}

void WaterTankApp::run()
{
    ESP_LOGI(TAG, "Starting application flow");

    // 1. Load state and statistics from persistent storage
    if (storage_.load(stats_) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load storage, using defaults");
        storage_.reset_to_defaults(stats_);
    }

    // 2. Perform sensor reading
    ultrasonic::Reading reading = sensor_.read_level();
    ESP_LOGI(TAG, "Reading raw: %.1f cm (Status: %d)", reading.cm, static_cast<int>(reading.result));

    // 3. Process logic (Brain)
    logic_.process_reading(reading, stats_);
    logic_.update_operation_mode(stats_);

    ESP_LOGI(TAG, "Result: %d, Distance: %.1f cm, Level: %d permille, Mode: %s", 
             static_cast<int>(stats_.last_result), 
             stats_.last_distance_cm, 
             stats_.level_permille,
             stats_.backup_mode_active ? "BACKUP" : "NORMAL");

    // 4. Save updated state
    storage_.save(stats_);

    // 5. Transmit data to Hub
    send_report();

    // 6. Calculate sleep and enter deep sleep
    uint64_t sleep_time_us = logic_.calculate_sleep_time_us(stats_);
    enter_deep_sleep(sleep_time_us);
}

void WaterTankApp::send_report()
{
    WaterLevelReport report = {};
    
    // Initialize header - Note: Some fields might be overwritten by EspNowManager
    report.header.sender_node_id = static_cast<uint8_t>(FarmNodeId::WATER_TANK);
    report.header.msg_type       = MessageType::DATA;
    report.header.payload_type   = static_cast<uint8_t>(FarmPayloadType::WATER_LEVEL_REPORT);

    report.level_permille = stats_.level_permille;
    report.distance_cm    = stats_.last_distance_cm;
    report.battery_mv     = 0; // TODO: Add battery monitor
    
    // Map UsResult to protocol's Quality and Failure
    // Based on Irrigation project protocol definitions (OK=0, WEAK=1, INVALID=2)
    if (stats_.last_result == ultrasonic::UsResult::OK) {
        report.quality = 0; // UsQuality::OK
        report.failure = 0; // UsFailure::NONE
    } else if (stats_.last_result == ultrasonic::UsResult::WEAK_SIGNAL) {
        report.quality = 1; // UsQuality::WEAK
        report.failure = 0; // UsFailure::NONE
    } else {
        report.quality = 2; // UsQuality::INVALID
        switch (stats_.last_result) {
            case ultrasonic::UsResult::TIMEOUT: 
                report.failure = 1; // UsFailure::TIMEOUT
                break;
            case ultrasonic::UsResult::ECHO_STUCK:
            case ultrasonic::UsResult::HW_FAULT: 
                report.failure = 2; // UsFailure::HW_ERROR
                break;
            case ultrasonic::UsResult::OUT_OF_RANGE: 
                report.failure = 3; // UsFailure::INVALID_PULSE
                break;
            case ultrasonic::UsResult::HIGH_VARIANCE:
            case ultrasonic::UsResult::INSUFFICIENT_SAMPLES: 
                report.failure = 4; // UsFailure::HIGH_VARIANCE
                break;
            default: 
                report.failure = 0; 
                break;
        }
    }

    report.float_switch_is_full = float_switch_.is_tank_full();
    report.backup_mode_active    = stats_.backup_mode_active;

    ESP_LOGI(TAG, "Sending report: %d permille", report.level_permille);
    
    esp_err_t err = comm_.send_data(
        0xFF, // BROADCAST_NODE_ID
        static_cast<uint8_t>(FarmPayloadType::WATER_LEVEL_REPORT),
        &report,
        sizeof(report),
        true // require_ack
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send report: %s", esp_err_to_name(err));
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
