#include "water_tank_app.hpp"
#include "esp_log.h"
#include "esp_sleep.h"
#include "app_protocol_types.hpp"

static const char *TAG = "WaterTankApp";

WaterTankApp::WaterTankApp(ILevelSensor &sensor, 
                           IFloatSwitch &float_switch, 
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
    float distance_cm = 0;
    uint8_t quality = 0;
    uint8_t failure = 0;
    sensor_.read_raw_distance_cm(distance_cm, quality, failure);
    ESP_LOGI(TAG, "Reading: %.1f cm (Q:%d, F:%d)", distance_cm, quality, failure);

    // 3. Process logic (Brain)
    logic_.process_reading(distance_cm, quality, failure, stats_);
    logic_.update_operation_mode(stats_);

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
    WaterLevelReport report;
    // Note: header is usually handled by the codec or initialized here
    report.header.source_node_id = static_cast<NodeId>(FarmNodeId::WATER_TANK);
    report.header.msg_type = MessageType::DATA;
    
    report.level_permille = stats_.level_permille;
    report.distance_cm = stats_.last_distance_cm;
    report.battery_mv = 0; // TODO: Add battery monitor
    report.quality = static_cast<uint8_t>(stats_.quality);
    report.failure = static_cast<uint8_t>(stats_.failure);
    report.float_switch_is_full = float_switch_.is_active();
    report.backup_mode_active = stats_.backup_mode_active;

    ESP_LOGI(TAG, "Sending report: %d permille", report.level_permille);
    
    esp_err_t err = comm_.send_data(
        BROADCAST_NODE_ID, // Or HUB_NODE_ID if known
        static_cast<PayloadType>(FarmPayloadType::WATER_LEVEL_REPORT),
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

    // Configure float switch wakeup if necessary
    if (float_switch_.should_enable_wakeup()) {
        // Note: In a real implementation, we would need the GPIO number here.
        // For this refactoring, we assume the adapter or logic handled the 
        // high-level decision, but the OS call still needs the pin.
        // In Stage 4, we might need a HAL for sleep too if we want 100% decoupling.
        // For now, we'll use direct ESP-IDF calls as allowed for the orchestrator.
        
        // TODO: Map this correctly to the physical GPIO
        // esp_deep_sleep_enable_gpio_wakeup(...);
    }

    esp_deep_sleep_start();
}
