#include "WaterTankApp.hpp"
#include "FloatSwitch.hpp"
// #include "HCSR04Gpio.hpp"
#include "HCSR04Rmt.hpp"
#include "NvsCore.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG // Must be call before esp_log.h
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

const char *TAG = "WaterTankApp";

RTC_DATA_ATTR uint16_t rtc_last_level_permille = 0;
RTC_DATA_ATTR bool     rtc_has_level           = false;

static const UltrasonicSensor::UltrasonicConfig cfg = {
    .ping_count       = 7,
    .ping_interval_ms = 70,
    .ping_duration_us = 20,
    .timeout_us       = 25000,
    .filter           = UltrasonicSensor::Filter::DOMINANT_CLUSTER,
    .blind_ping       = true};

// HCSR04Gpio sensor(GPIO_NUM_21, GPIO_NUM_19, cfg);
HCSR04Rmt sensor(GPIO_NUM_21, GPIO_NUM_19, cfg);

static FloatSwitch::Config fs_cfg = {.pin           = GPIO_NUM_4, // exemplo
                                     .normally_open = true,
                                     .pull          = FloatSwitch::Pull::UP,
                                     .debounce_ms   = 50,
                                     .wakeup_edge   = FloatSwitch::WakeupLevel::LOW};

static FloatSwitch floatswitch(fs_cfg);

FillState WaterTankApp::infer_fill_state(uint16_t current_level)
{
    if (!rtc_has_level)
    {
        rtc_last_level_permille = current_level;
        rtc_has_level           = true;
        ESP_LOGD(TAG, "fill_state:Unknown");
        return FillState::UNKNOWN;
    }

    int delta = (int)current_level - (int)rtc_last_level_permille;

    rtc_last_level_permille = current_level;

    if (delta > +LEVEL_DELTA_MIN)
    {
        ESP_LOGD(TAG, "fill_state:Filling, D:%d", delta);
        return FillState::FILLING;
    }
    if (delta < -LEVEL_DELTA_MIN)
    {
        ESP_LOGD(TAG, "fill_state:Draining, D:%d", delta);
        return FillState::DRAINING;
    }

    ESP_LOGD(TAG, "fill_state:Stable, D:%d", delta);
    return FillState::STABLE;
}

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

void WaterTankApp::configure_sleep_policy(bool boia, uint64_t timer_us)
{
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

    // Timer sempre que solicitado
    if (timer_us > 0)
    {
        esp_sleep_enable_timer_wakeup(timer_us);
    }

    // Wake por boia APENAS se boia estiver aberta
    if (!boia)
    {
        FloatSwitch::WakeupInfo wi;
        if (floatswitch.get_wakeup_info(wi))
        {
            esp_sleep_enable_ext0_wakeup(wi.pin, wi.level);
        }
    }
}

static uint16_t distance_to_level_permille(float d_cm)
{
    if (d_cm >= LEVEL_MIN_CM)
        return 0;

    if (d_cm <= LEVEL_MAX_CM)
        return 1000;

    float span  = LEVEL_MIN_CM - LEVEL_MAX_CM;
    float level = (LEVEL_MIN_CM - d_cm) / span;

    return static_cast<uint16_t>(level * 1000.0f);
}

void WaterTankApp::init()
{
    ESP_LOGI(TAG, "Initializing WaterTankApp");

    NvsCore::init();
    NvsCore::load();

    auto &core = NvsCore::data();

    esp_reset_reason_t       reset_reason = esp_reset_reason();
    esp_sleep_wakeup_cause_t wake_cause   = esp_sleep_get_wakeup_cause();
    ESP_LOGI(TAG, "Wakeup cause: %d", wake_cause);

    bool woke_from_sleep =
        (wake_cause == ESP_SLEEP_WAKEUP_EXT0) || (wake_cause == ESP_SLEEP_WAKEUP_EXT1) ||
        (wake_cause == ESP_SLEEP_WAKEUP_TIMER) || (wake_cause == ESP_SLEEP_WAKEUP_TOUCHPAD) ||
        (wake_cause == ESP_SLEEP_WAKEUP_ULP);

    if (!core.ota.last_boot_ok && !woke_from_sleep)
    {
        core.lifecycle.crash_count++;
    }

    core.lifecycle.boot_count++;
    core.ota.last_boot_ok = false;

    switch (wake_cause)
    {
    case ESP_SLEEP_WAKEUP_EXT0:
    case ESP_SLEEP_WAKEUP_EXT1:
        core.wake.last_wake = WakeSource::GPIO;
        break;

    case ESP_SLEEP_WAKEUP_TIMER:
        core.wake.last_wake = WakeSource::TIMER;
        break;

    case ESP_SLEEP_WAKEUP_UNDEFINED:
        core.wake.last_wake = WakeSource::POWER_ON;
        break;

    default:
        core.wake.last_wake = WakeSource::SOFTWARE;
        break;
    }

    core.wake.next_wake_hint = WakeHint::NONE;

    NvsCore::commit();

    sensor.init();
    floatswitch.init();

    vTaskDelay(pdMS_TO_TICKS(100));

    FloatSwitch::WakeupInfo wi;
    if (floatswitch.get_wakeup_info(wi))
    {
        ESP_LOGI(TAG, "Config wakeup: pin=%d level=%d", wi.pin, wi.level);
        esp_sleep_enable_ext0_wakeup(wi.pin, wi.level);
    }
}

void WaterTankApp::run()
{
    ESP_LOGI(TAG, "Run");

    while (true)
    {
        float distance = 0.0f;

        if (!sensor.readDistanceCm(distance))
        {
            ESP_LOGW(TAG, "sensor error");
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        uint16_t  level      = distance_to_level_permille(distance);
        FillState fill_state = infer_fill_state(level);

        // Configura sono
        uint64_t timer_us = decide_timer_us(fill_state);
        bool     boia     = floatswitch.read();

        configure_sleep_policy(boia, timer_us);

        ESP_LOGI(TAG, "Level=%u, Distance=%.2f cm. Timer=%u", level, distance, timer_us / 1000000U);

        // TEMPORÁRIO para debug
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
