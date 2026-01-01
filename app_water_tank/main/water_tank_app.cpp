#include "water_tank_app.hpp"
#include "float_switch.hpp"
#include "nvs_core.hpp"
#include "power_control.hpp"
#include "ultrasonic_sensor.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define POWER_GPIO GPIO_NUM_20
#define TRIGER_GPIO GPIO_NUM_21
#define ECHO_GPIO GPIO_NUM_7
#define FLOAT_SWITCH_GPIO GPIO_NUM_1
#define BATT_LEVEL_GPIO GPIO_NUM_0

static const char *TAG = "WaterTankApp";

// --- Constants ---
static constexpr uint32_t ULTRASONIC_WARMUP_MS          = 600;
static constexpr uint8_t CONSECUTIVE_FAILURES_THRESHOLD = 5;
static constexpr uint64_t BACKUP_MODE_SLEEP_US = 15ULL * 1000000ULL; // 15 seconds

// --- Persistence in RTC Memory ---
RTC_DATA_ATTR CoreStorage rtc_core_data;

WaterTankApp::WaterTankApp()
{
}

static constexpr PowerControl::Config power_cfg = {
    .gpio           = POWER_GPIO,
    .inverted_logic = true,
    .initial_on     = false,
};
static PowerControl power_to_sensor(power_cfg);

// --- Hardware Configurations ---
static UltrasonicSensor::UltrasonicConfig us_cfg = {
    .ping_count       = 9,
    .ping_interval_ms = 70,
    .ping_duration_us = 20,
    .timeout_us       = 25000,
    .filter           = UltrasonicSensor::Filter::DOMINANT_CLUSTER,
    .blind_ping       = false,
    .min_distance_cm  = 10.0f,
    .max_distance_cm  = 200.0f,
    .warmup_time_ms   = 600};

static UltrasonicSensor sensor(TRIGER_GPIO, ECHO_GPIO, us_cfg);

static FloatSwitch::Config fs_cfg = {
    .gpio          = FLOAT_SWITCH_GPIO,
    .normally_open = true,
    .active_level  = FloatSwitch::ActiveLevel::LOW,
    .wakeup_on     = FloatSwitch::WakeupCondition::WHEN_TANK_IS_EMPTY,
};
static FloatSwitch floatswitch(fs_cfg);

// --- Business Logic ---
void WaterTankApp::updateOperationMode()
{
    auto &stats = storage_.stats;

    // This function implements the state machine for the backup mode.
    if (stats.quality == UsQuality::INVALID) {
        // Increment consecutive failures if the reading is invalid.
        if (stats.consecutive_failures < CONSECUTIVE_FAILURES_THRESHOLD) {
            stats.consecutive_failures++;
        }
    }
    else if (stats.quality == UsQuality::OK) {
        // Decrement on OK readings to add hysteresis.
        if (stats.consecutive_failures > 0) {
            stats.consecutive_failures--;
        }
    }
    // Note: WEAK readings do not change the counter.

    // Update the backup mode state based on the threshold.
    if (stats.consecutive_failures >= CONSECUTIVE_FAILURES_THRESHOLD) {
        stats.backup_mode_active = true;
    }
    else if (stats.consecutive_failures < CONSECUTIVE_FAILURES_THRESHOLD - 1) {
        stats.backup_mode_active = false;
    }
}

uint64_t WaterTankApp::decideSleepTimeUs()
{
    auto &app_stats = storage_.stats;

    // If backup mode is active, the float switch dictates the sleep cycle.
    if (app_stats.backup_mode_active) {
        // If the tank is not full (i.e., it's empty or filling),
        // use a short sleep time to poll the float switch frequently.
        if (!floatswitch.isTankFull()) {
            return BACKUP_MODE_SLEEP_US;
        }
        // If the tank is full, revert to a long, stable sleep time.
        else {
            return TIMER_STABLE_US;
        }
    }

    // Normal operation mode: sleep time is based on ultrasonic sensor readings.
    uint64_t timer_us = 0;
    switch (app_stats.quality) {
    case UsQuality::OK:
    case UsQuality::WEAK:
        // For OK or WEAK quality, the sleep time is based on the fill state.
        switch (app_stats.fill_state) {
        case FillState::FILLING:
            timer_us = TIMER_FILLING_US;
            break;
        case FillState::DRAINING:
            timer_us = TIMER_DRAIN_US;
            break;
        case FillState::STABLE:
            timer_us = TIMER_STABLE_US;
            break;
        case FillState::UNKNOWN:
        default:
            timer_us = TIMER_UNKNOWN_US;
            break;
        }
        if (app_stats.quality == UsQuality::WEAK) {
            timer_us *= WEAK_SLEEP_FACTOR;
        }
        sensor.setPingCount(us_cfg.ping_count);
        break;
    case UsQuality::INVALID:
    default:
        // For INVALID quality, use a shorter sleep time to retry sooner.
        timer_us = TIMER_UNKNOWN_US * INVALID_SLEEP_FACTOR;
        sensor.setPingCount(us_cfg.ping_count + 4);
        break;
    }

    return timer_us;
}

void WaterTankApp::configureSleepPolicy(uint64_t timer_us)
{
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

    if (timer_us > 0) {
        esp_sleep_enable_timer_wakeup(timer_us);
    }

    // Use the new, safe method from FloatSwitch to decide if GPIO wakeup should be armed.
    // This prevents wake-up loops.
    if (floatswitch.shouldEnableWakeup()) {
        // The wakeup level (HIGH/LOW) depends on the physical connection (active_level).
        esp_deepsleep_gpio_wake_up_mode_t wakeup_mode =
            (fs_cfg.active_level == FloatSwitch::ActiveLevel::HIGH)
                ? ESP_GPIO_WAKEUP_GPIO_HIGH
                : ESP_GPIO_WAKEUP_GPIO_LOW;

        esp_deep_sleep_enable_gpio_wakeup(1ULL << fs_cfg.gpio, wakeup_mode);
        storage_.stats.gpio_wakeup_enabled = true;
        ESP_LOGI(TAG, "GPIO wakeup enabled");
    }
    else {
        storage_.stats.gpio_wakeup_enabled = false;
        ESP_LOGI(TAG, "GPIO wakeup disabled by logic");
    }
}

static uint16_t distance_to_level_permille(float d_cm)
{
    if (d_cm >= LEVEL_MIN_CM) {
        return 0;
    }
    if (d_cm <= LEVEL_MAX_CM) {
        return 1000;
    }
    float span  = LEVEL_MIN_CM - LEVEL_MAX_CM;
    float level = (LEVEL_MIN_CM - d_cm) / span;
    return static_cast<uint16_t>(level * 1000.0f);
}

WaterLevelReport WaterTankApp::createWaterLevelReport()
{
    power_to_sensor.on();
    float distance_cm = 0.0f;
    UsQuality quality = UsQuality::INVALID;
    UsFailure failure = UsFailure::NONE;

    // In backup mode, we still attempt to read the sensor to see if it recovers.
    sensor.readDistance_cm(distance_cm, quality, failure);

    // If the sensor read is invalid, use the last reading.
    if (distance_cm == 0.0f && quality == UsQuality::INVALID) {
        distance_cm = storage_.stats.last_distance_cm;
    }

    power_to_sensor.off();

    // Populate the report with all available data.
    WaterLevelReport report = {
        .level_permille       = distance_to_level_permille(distance_cm),
        .distance_cm          = distance_cm,
        .quality              = quality,
        .failure              = failure,
        .float_switch_is_full = floatswitch.isTankFull(),
        .backup_mode_active   = storage_.stats.backup_mode_active,
    };

    return report;
}

void WaterTankApp::sendWaterLevelReport(const WaterLevelReport &report)
{
    auto peers = comm_.getPeers();

    if (peers.empty()) {
        if (!comm_.broadcast(reinterpret_cast<const uint8_t *>(&report),
                             sizeof(report))) {
            ESP_LOGE(TAG, "Failed to send broadcast");
        }
    }
    else {
        for (const auto &peer : peers) {
            // Require ACK by setting the last parameter to true
            if (!comm_.send(peer.node_id, reinterpret_cast<const uint8_t *>(&report),
                            sizeof(report), true)) {
                ESP_LOGE(TAG, "Failed to send to peer %u", peer.node_id);
            }
        }
    }
}

void WaterTankApp::onEspNowReceive(uint8_t node_id,
                                   const uint8_t *data,
                                   int len,
                                   int8_t rssi)
{
    ESP_LOGI(TAG, "Received %d bytes from node %u (RSSI: %d dBm)", len, node_id, rssi);
    // This device is a sensor, so it primarily sends data.
}

void WaterTankApp::onEspNowSend(uint8_t node_id, esp_now_send_status_t status)
{
    const char *status_str = (status == ESP_NOW_SEND_SUCCESS) ? "SUCCESS" : "FAIL";

    if (node_id == 0xFF) {
        ESP_LOGI(TAG, "Broadcast send: %s", status_str);
    }
    else {
        ESP_LOGI(TAG, "Send to node %u: %s", node_id, status_str);
    }
}

void WaterTankApp::onAckSuccess(uint8_t node_id)
{
    ESP_LOGI(TAG, "ACK received from node %u", node_id);
}

void WaterTankApp::onAckTimeout(uint8_t node_id)
{
    ESP_LOGW(TAG, "ACK timeout for node %u", node_id);
}

void WaterTankApp::init()
{
    ESP_LOGI(TAG, "Initializing WaterTankApp");
    storage_.init_partition();

    power_to_sensor.init();
    sensor.init();
    floatswitch.init();

    if (storage_.load() != ESP_OK) {
        ESP_LOGW(TAG, "NVS load failed, performing factory reset");
        storage_.factory_reset();
    }

    auto &core = storage_.getCoreData();
    if (core.node_id == 0) {
        ESP_LOGI(TAG, "Node ID not set, generating from MAC address...");
        uint8_t mac[6];
        esp_efuse_mac_get_default(mac);
        core.node_id = mac[3] ^ mac[4] ^ mac[5];
        if (storage_.commit() == ESP_OK) {
            ESP_LOGI(TAG, "New Node ID saved to NVS.");
        }
        else {
            ESP_LOGE(TAG, "Failed to save new Node ID to NVS!");
        }
    }

    // Initialize ESP-NOW communication
    ESPNOWConfig config;
    config.wifi_channel       = 0;
    config.max_peers          = 10;
    config.ack_timeout        = 100;
    config.heartbeat_interval = 0;
    config.max_packet_size    = 250;

    if (!comm_.init(config, common::NodeType::WATER_TANK)) {
        ESP_LOGE(TAG, "Failed to initialize ESP-NOW");
    }
    else {
        ESP_LOGI(TAG, "ESP-NOW initialized. Our node ID: %u", comm_.get_id());
        comm_.setReceiveCallback(
            [this](uint8_t node_id, const uint8_t *data, int len, int8_t rssi) {
                this->onEspNowReceive(node_id, data, len, rssi);
            });
        comm_.setSendCallback([this](uint8_t node_id, esp_now_send_status_t status) {
            this->onEspNowSend(node_id, status);
        });
        comm_.setAckSuccessCallback(
            [this](uint8_t node_id) { this->onAckSuccess(node_id); });
        comm_.setAckTimeoutCallback(
            [this](uint8_t node_id) { this->onAckTimeout(node_id); });
        comm_.startDiscovery(10000);
    }

    // System status and wakeup reason logging
    esp_reset_reason_t reset_reason     = esp_reset_reason();
    esp_sleep_wakeup_cause_t wake_cause = esp_sleep_get_wakeup_cause();
    ESP_LOGI(TAG, "Reset reason: %d, Wakeup cause: %d", reset_reason, wake_cause);

    bool woke_from_sleep = (wake_cause != ESP_SLEEP_WAKEUP_UNDEFINED);

    if (!woke_from_sleep && core.boot_count > 0) {
        core.crash_count++;
    }
    core.boot_count++;

    switch (wake_cause) {
    case ESP_SLEEP_WAKEUP_EXT0:
    case ESP_SLEEP_WAKEUP_EXT1:
        core.last_wake = WakeSource::GPIO;
        ESP_LOGI(TAG, "Woke from GPIO");
        break;
    case ESP_SLEEP_WAKEUP_TIMER:
        core.last_wake = WakeSource::TIMER;
        ESP_LOGI(TAG, "Woke from TIMER");
        break;
    case ESP_SLEEP_WAKEUP_UNDEFINED:
    default:
        core.last_wake = WakeSource::POWER_ON;
        ESP_LOGI(TAG, "Power-on reset or undefined wake");
        break;
    }

    rtc_core_data = core;
    ESP_LOGI(TAG, "Boot #%lu (crashes: %lu)", rtc_core_data.boot_count,
             rtc_core_data.crash_count);

    vTaskDelay(pdMS_TO_TICKS(100));
}

void WaterTankApp::run()
{
    ESP_LOGI(TAG, "Starting Application Loop");

    while (true) {
        // 1. Create a comprehensive report with sensor and float switch data.
        WaterLevelReport report = createWaterLevelReport();

        // 2. Update application state and statistics based on the new report.
        storage_.updateStatus(report.level_permille, report.distance_cm, report.quality,
                              report.failure);

        // 3. Update the operation mode (normal vs. backup).
        updateOperationMode();

        // 4. Decide sleep time based on the current state.
        uint64_t timer_us = decideSleepTimeUs();

        // 5. Configure sleep hardware (timer and GPIO).
        configureSleepPolicy(timer_us);

        // 6. Send the report via ESP-NOW.
        sendWaterLevelReport(report);

        // 7. Process any pending communication tasks.
        comm_.process();

        // 8. Persist essential data to RTC memory before sleeping.
        auto &core_data            = storage_.getCoreData();
        core_data.sleep_interval_s = (uint32_t)(timer_us / 1000000ULL);
        rtc_core_data              = core_data;

        // 9. Log summary and enter deep sleep.
        auto &app_stats = storage_.stats;
        ESP_LOGI(TAG,
                 "Summary - Level: %u‰, Dist: %.2fcm, Quality: %d, Failure: %d, "
                 "State: %d",
                 app_stats.level_permille, app_stats.last_distance_cm,
                 (int)app_stats.quality, (int)app_stats.failure,
                 (int)app_stats.fill_state);
        ESP_LOGI(TAG, "Stats - Total: %lu, OK: %lu, WEAK: %lu, INV: %lu, Timeout: %lu",
                 app_stats.measure_count, app_stats.ok_count, app_stats.weak_count,
                 app_stats.invalid_count, app_stats.timeout_count);
        ESP_LOGI(TAG, "Core - Boot: %lu, Crash: %lu", core_data.boot_count,
                 core_data.crash_count);
        ESP_LOGI(TAG, "Mode - Backup: %s, Consecutive Fails: %u",
                 app_stats.backup_mode_active ? "YES" : "NO",
                 app_stats.consecutive_failures);
        ESP_LOGI(TAG, "Entering deep sleep for %lu seconds", core_data.sleep_interval_s);

        vTaskDelay(pdMS_TO_TICKS(2000));
        // esp_deep_sleep_start();
    }
}
