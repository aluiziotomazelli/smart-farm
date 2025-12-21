#include "water_tank_app.hpp"
#include "FloatSwitch.hpp"
#include "HCSR04Rmt.hpp"
#include "NvsCore.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "WaterTankApp";

// --- Persistence in RTC Memory ---
// These variables survive Deep Sleep but are lost on Power-On Reset.
RTC_DATA_ATTR CoreStorage rtc_core_data;               // Caching for Core data to reduce NVS writes
RTC_DATA_ATTR uint16_t    rtc_last_level_permille = 0; // Used for filling/draining trend
RTC_DATA_ATTR bool        rtc_has_level           = false;

WaterTankApp::WaterTankApp()
    : _sensor_power({.enable_gpio = GPIO_NUM_25, .active_high = true, .initial_on = false})
{
}

// --- Hardware Configurations ---

// Ultrasonic Sensor (HC-SR04) Configuration
static const UltrasonicSensor::UltrasonicConfig cfg = {
    .ping_count       = 9,
    .ping_interval_ms = 70,
    .ping_duration_us = 20,
    .timeout_us       = 35000,
    .filter           = UltrasonicSensor::Filter::DOMINANT_CLUSTER,
    .blind_ping       = true};

static HCSR04Rmt sensor(GPIO_NUM_21, GPIO_NUM_19, cfg);

// Float Switch (Mechanical Overflow/Top Level) Configuration
static FloatSwitch::Config fs_cfg = {.pin           = GPIO_NUM_4,
                                     .normally_open = true,
                                     .pull          = FloatSwitch::Pull::UP,
                                     .debounce_ms   = 50,
                                     .wakeup_edge   = FloatSwitch::WakeupLevel::LOW};

static FloatSwitch floatswitch(fs_cfg);

// --- Business Logic ---

/**
 * Determines if the tank level is rising, falling, or stable
 * by comparing current reading with the previous one stored in RTC RAM.
 */
FillState WaterTankApp::infer_fill_state(uint16_t current_level)
{
    if (!rtc_has_level)
    {
        rtc_last_level_permille = current_level;
        rtc_has_level           = true;
        ESP_LOGD(TAG, "Fill state: Unknown (First reading)");
        return FillState::UNKNOWN;
    }

    int delta               = (int)current_level - (int)rtc_last_level_permille;
    rtc_last_level_permille = current_level;

    if (delta > +LEVEL_DELTA_MIN)
    {
        ESP_LOGD(TAG, "Fill state: Filling, Delta: %d", delta);
        return FillState::FILLING;
    }
    if (delta < -LEVEL_DELTA_MIN)
    {
        ESP_LOGD(TAG, "Fill state: Draining, Delta: %d", delta);
        return FillState::DRAINING;
    }

    ESP_LOGD(TAG, "Fill state: Stable, Delta: %d", delta);
    return FillState::STABLE;
}

/**
 * Selects the appropriate sleep interval based on the current fill state.
 * Faster intervals for Filling/Draining, longer for Stable.
 */
uint64_t WaterTankApp::decide_timer_us(FillState state)
{
    switch (state)
    {
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

/**
 * Configures the ESP32 Wakeup sources.
 * Always sets a Timer wakeup and optionally sets a GPIO wakeup for the float switch.
 */
void WaterTankApp::configure_sleep_policy(bool float_switch_closed, uint64_t timer_us)
{
    // Clear previous settings
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

    // Enable Timer wakeup if interval is valid
    if (timer_us > 0)
    {
        esp_sleep_enable_timer_wakeup(timer_us);
    }

    // Enable External Wakeup (Float Switch) ONLY if currently open.
    // This allows the device to wake up immediately if the tank hits the top limit.
    if (!float_switch_closed)
    {
        FloatSwitch::WakeupInfo wi;
        if (floatswitch.get_wakeup_info(wi))
        {
            esp_sleep_enable_ext0_wakeup(wi.pin, wi.level);
        }
    }
}

/**
 * Converts distance in cm to tank level in parts per thousand (permille 0-1000).
 * Uses LEVEL_MIN_CM (Empty) and LEVEL_MAX_CM (Full) as boundaries.
 */
static uint16_t distance_to_level_permille(float d_cm)
{
    // Distance greater than empty threshold = 0%
    if (d_cm >= LEVEL_MIN_CM)
        return 0;

    // Distance smaller than full threshold = 100%
    if (d_cm <= LEVEL_MAX_CM)
        return 1000;

    // Linear interpolation
    float span  = LEVEL_MIN_CM - LEVEL_MAX_CM;
    float level = (LEVEL_MIN_CM - d_cm) / span;

    return static_cast<uint16_t>(level * 1000.0f);
}

void WaterTankApp::init()
{
    // === NVS Maintenance (Keep commented for production) ===
    // _storage.factory_reset(); // Resets struct to defaults and saves
    // nvs_flash_erase();        // Deep erase of the entire partition
    // nvs_flash_init();         // Re-initialize after deep erase

    ESP_LOGI(TAG, "Initializing WaterTankApp");
    _storage.init_partition();

    // === Hardware Initialization ===
    ESP_ERROR_CHECK(_sensor_power.init());
    sensor.init();
    floatswitch.init();

    // === NVS Storage Initialization ===
    // This now handles first boot or corrupted data automatically
    if (_storage.load() != ESP_OK)
    {
        ESP_LOGW(TAG, "NVS load failed, performing factory reset");
        _storage.factory_reset();
    }

    // Direct access to core and app data references
    auto &core = _storage.getCoreData();
    // auto &app  = _storage.stats;

    // === Boot and Wakeup Detection ===
    esp_reset_reason_t       reset_reason = esp_reset_reason();
    esp_sleep_wakeup_cause_t wake_cause   = esp_sleep_get_wakeup_cause();

    ESP_LOGI(TAG, "Reset reason: %d, Wakeup cause: %d", reset_reason, wake_cause);

    bool woke_from_sleep =
        (wake_cause == ESP_SLEEP_WAKEUP_EXT0) || (wake_cause == ESP_SLEEP_WAKEUP_EXT1) ||
        (wake_cause == ESP_SLEEP_WAKEUP_TIMER) || (wake_cause == ESP_SLEEP_WAKEUP_TOUCHPAD) ||
        (wake_cause == ESP_SLEEP_WAKEUP_ULP);

    // === Lifecycle Management ===
    // If it's not a sleep wakeup and boot_count > 0, it's likely a crash/hard reset
    if (!woke_from_sleep)
    {
        if (core.boot_count > 0)
            core.crash_count++;
    }
    core.boot_count++;

    // === Identify Wakeup Source ===
    switch (wake_cause)
    {
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
        core.last_wake = WakeSource::POWER_ON;
        ESP_LOGI(TAG, "Power-on reset or undefined wake");
        break;

    default:
        core.last_wake = WakeSource::NONE;
        break;
    }

    // === Cache Core Data to RTC ===
    // To avoid writing to NVS twice per cycle, we cache the updated core data
    // in RTC memory and commit it once at the end of the `run` cycle.
    rtc_core_data = core;

    ESP_LOGI(TAG, "Boot #%lu (crashes: %lu)", rtc_core_data.boot_count, rtc_core_data.crash_count);

    // Brief settling delay
    vTaskDelay(pdMS_TO_TICKS(100));
}

void WaterTankApp::run()
{
    ESP_LOGI(TAG, "Starting Application Loop");

    while (true)
    {
        // Direct access to storage references
        auto &core = _storage.getCoreData();
        auto &app  = _storage.stats;

        float     distance = 0.0f;
        UsQuality quality  = UsQuality::INVALID;
        UsFailure failure  = UsFailure::NONE;
        uint16_t  level    = 0;

        // === 1. Sensor Power On and Reading ===
        _sensor_power.on();
        vTaskDelay(pdMS_TO_TICKS(ULTRASONIC_WARMUP_MS));
        bool ok = sensor.readDistanceCm(distance, quality, failure);
        _sensor_power.off();

        if (ok)
        {
            level = distance_to_level_permille(distance);

            if (quality == UsQuality::WEAK)
            {
                ESP_LOGW(TAG, "Ultrasonic WEAK reading: %.2f cm (level: %u‰)", distance, level);
            }
            else
            {
                ESP_LOGI(TAG, "Distance: %.2f cm, Level: %u‰", distance, level);
            }
        }
        else
        {
            ESP_LOGW(TAG, "Ultrasonic INVALID reading (failure code: %d)", (int)failure);
        }

        // === 2. Update Application Statistics ===
        _storage.updateStatus(level, distance, quality, failure);
        app.fill_state = infer_fill_state(level);

        // === 3. Sleep Interval Calculation ===
        uint64_t timer_us = decide_timer_us(app.fill_state);

        // Apply quality factors to the sleep timer
        if (quality == UsQuality::WEAK)
            timer_us = static_cast<uint64_t>(timer_us * WEAK_SLEEP_FACTOR);
        else if (quality == UsQuality::INVALID)
            timer_us = static_cast<uint64_t>(timer_us * INVALID_SLEEP_FACTOR);

        // Update core sleep interval (common telemetry)
        core.sleep_interval_s          = (uint32_t)(timer_us / 1000000ULL);
        rtc_core_data.sleep_interval_s = core.sleep_interval_s;

        // === 4. Configure Sleep Policy (GPIO Wakeup) ===
        bool float_switch_closed = floatswitch.read();
        app.gpio_wakeup_enabled  = !float_switch_closed;
        configure_sleep_policy(float_switch_closed, timer_us);

        // === 5. Update Sample Timestamp ===
        app.sample_uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);

        // === 6. Persist Data to NVS ===
        // Restore core data from RTC memory before the final commit
        core = rtc_core_data;
        // _storage.commit();

        // === 7. Summary Log ===
        ESP_LOGI(
            TAG,
            "Summary - Level: %u‰, Measures: %lu (OK:%lu WEAK:%lu INV:%lu), Boot: %lu, Crash: %lu",
            app.level_permille, app.measure_count, app.ok_count, app.weak_count, app.invalid_count,
            core.boot_count, core.crash_count);

        // === 8. Deep Sleep or Loop Delay ===
        // For production, replace delay with: esp_deep_sleep_start();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}