#include "water_tank_app.hpp"
#include "float_switch.hpp"
#include "nvs_core.hpp"
#include "trig_echo_rmt.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "WaterTankApp";

// --- Persistence in RTC Memory ---
RTC_DATA_ATTR CoreStorage rtc_core_data;
RTC_DATA_ATTR uint16_t    rtc_last_level_permille = 0;
RTC_DATA_ATTR bool        rtc_has_level           = false;

WaterTankApp::WaterTankApp()
    : sensor_power_({.enable_gpio = GPIO_NUM_25, .active_high = true, .initial_on = false})
    , comm_(comm::CommInterface::get_default_instance())
{
}

// --- Hardware Configurations ---
static const UltrasonicSensor::UltrasonicConfig cfg = {
    .ping_count       = 9,
    .ping_interval_ms = 70,
    .ping_duration_us = 20,
    .timeout_us       = 35000,
    .filter           = UltrasonicSensor::Filter::DOMINANT_CLUSTER,
    .blind_ping       = true};

static TrigerEchoRmt sensor(GPIO_NUM_21, GPIO_NUM_19, cfg);

static FloatSwitch::Config fs_cfg = {.pin           = GPIO_NUM_4,
                                     .normally_open = true,
                                     .pull          = FloatSwitch::Pull::UP,
                                     .debounce_ms   = 50,
                                     .wakeup_edge   = FloatSwitch::WakeupLevel::LOW};
static FloatSwitch floatswitch(fs_cfg);

// --- Business Logic ---
FillState WaterTankApp::infer_fill_state(uint16_t current_level)
{
    if (!rtc_has_level) {
        rtc_last_level_permille = current_level;
        rtc_has_level           = true;
        ESP_LOGD(TAG, "Fill state: Unknown (First reading)");
        return FillState::UNKNOWN;
    }

    int delta               = (int)current_level - (int)rtc_last_level_permille;
    rtc_last_level_permille = current_level;

    if (delta > +LEVEL_DELTA_MIN) {
        ESP_LOGD(TAG, "Fill state: Filling, Delta: %d", delta);
        return FillState::FILLING;
    }
    if (delta < -LEVEL_DELTA_MIN) {
        ESP_LOGD(TAG, "Fill state: Draining, Delta: %d", delta);
        return FillState::DRAINING;
    }

    ESP_LOGD(TAG, "Fill state: Stable, Delta: %d", delta);
    return FillState::STABLE;
}

uint64_t WaterTankApp::decide_timer_us(FillState state)
{
    switch (state) {
    case FillState::FILLING:
        return TIMER_FILLING_US;
    case FillState::DRAINING:
        return TIMER_DRAIN_US;
    case FillState::STABLE:
        return TIMER_STABLE_US;
    case FillState::UNKNOWN:
    default:
        return TIMER_UNKNOWN_US;
    }
}

void WaterTankApp::configure_sleep_policy(bool float_switch_closed, uint64_t timer_us)
{
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

    if (timer_us > 0) {
        esp_sleep_enable_timer_wakeup(timer_us);
    }

    if (!float_switch_closed) {
        FloatSwitch::WakeupInfo wi;
        if (floatswitch.get_wakeup_info(wi)) {
            esp_sleep_enable_ext0_wakeup(wi.pin, wi.level);
        }
    }
}

static uint16_t distance_to_level_permille(float d_cm)
{
    if (d_cm >= LEVEL_MIN_CM) return 0;
    if (d_cm <= LEVEL_MAX_CM) return 1000;
    float span  = LEVEL_MIN_CM - LEVEL_MAX_CM;
    float level = (LEVEL_MIN_CM - d_cm) / span;
    return static_cast<uint16_t>(level * 1000.0f);
}

void WaterTankApp::on_comm_receive(uint32_t source_node_id, const uint8_t* payload, size_t len)
{
    ESP_LOGI(TAG, "Received message from node 0x%lX (%d bytes)", source_node_id, len);
}


void WaterTankApp::init()
{
    ESP_LOGI(TAG, "Initializing WaterTankApp");
    storage_.init_partition();

    ESP_ERROR_CHECK(sensor_power_.init());
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
        core.node_id = (mac[2] << 24) | (mac[3] << 16) | (mac[4] << 8) | mac[5];
        ESP_LOGI(TAG, "New Node ID: 0x%08lX", core.node_id);
        if (storage_.commit() == ESP_OK) {
            ESP_LOGI(TAG, "New Node ID saved to NVS.");
        } else {
            ESP_LOGE(TAG, "Failed to save new Node ID to NVS!");
        }
    }

    esp_reset_reason_t reset_reason = esp_reset_reason();
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
    ESP_LOGI(TAG, "Boot #%lu (crashes: %lu)", rtc_core_data.boot_count, rtc_core_data.crash_count);

    // === NEW Communication Component Initialization ===
    if (!comm_.init(core.node_id)) {
        ESP_LOGE(TAG, "Failed to initialize communication component");
    } else {
        comm_.set_rx_callback([this](uint32_t source_node_id, const uint8_t* payload, size_t len) {
            this->on_comm_receive(source_node_id, payload, len);
        });
        ESP_LOGI(TAG, "Communication component initialized");
    }

    vTaskDelay(pdMS_TO_TICKS(100));
}

void WaterTankApp::run()
{
    ESP_LOGI(TAG, "Starting Application Loop");

    while (true) {
        auto &core = storage_.getCoreData();
        auto &app  = storage_.stats;

        float distance = 0.0f;
        UsQuality quality = UsQuality::INVALID;
        UsFailure failure = UsFailure::NONE;
        uint16_t level = 0;

        sensor_power_.on();
        vTaskDelay(pdMS_TO_TICKS(ULTRASONIC_WARMUP_MS));
        bool ok = sensor.read_distance_cm(distance, quality, failure);
        sensor_power_.off();

        if (ok) {
            level = distance_to_level_permille(distance);
            if (quality == UsQuality::WEAK) {
                ESP_LOGW(TAG, "Ultrasonic WEAK reading: %.2f cm (level: %u‰)", distance, level);
            } else {
                ESP_LOGI(TAG, "Distance: %.2f cm, Level: %u‰", distance, level);
            }
        } else {
            ESP_LOGW(TAG, "Ultrasonic INVALID reading (failure code: %d)", (int)failure);
        }

        storage_.updateStatus(level, distance, quality, failure);
        app.fill_state = infer_fill_state(level);

        uint64_t timer_us = decide_timer_us(app.fill_state);
        if (quality == UsQuality::WEAK) timer_us = static_cast<uint64_t>(timer_us * WEAK_SLEEP_FACTOR);
        else if (quality == UsQuality::INVALID) timer_us = static_cast<uint64_t>(timer_us * INVALID_SLEEP_FACTOR);
        core.sleep_interval_s = (uint32_t)(timer_us / 1000000ULL);
        rtc_core_data.sleep_interval_s = core.sleep_interval_s;

        bool float_switch_closed = floatswitch.read();
        app.gpio_wakeup_enabled  = !float_switch_closed;
        configure_sleep_policy(float_switch_closed, timer_us);

        app.sample_uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);

        // === Send Data using new Comm Interface ===
        uint16_t payload = app.level_permille;
        // Using 0xFFFFFFFF as broadcast node_id for testing
        comm_.send(0xFFFFFFFF, reinterpret_cast<const uint8_t*>(&payload), sizeof(payload));

        core = rtc_core_data;
        // _storage.commit();

        ESP_LOGI(TAG,
                 "Summary - Level: %u‰, Measures: %lu (OK:%lu WEAK:%lu INV:%lu), Boot: %lu, Crash: %lu",
                 app.level_permille, app.measure_count, app.ok_count, app.weak_count,
                 app.invalid_count, core.boot_count, core.crash_count);

        vTaskDelay(pdMS_TO_TICKS(2000));
        // esp_deep_sleep_start();
    }
}
