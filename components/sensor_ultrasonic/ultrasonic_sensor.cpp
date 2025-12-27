#include "ultrasonic_sensor.hpp"
#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"
#include "esp_timer.h"
#include <algorithm>
#include <cmath>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "UltrasonicSensor";

UltrasonicSensor::UltrasonicSensor(gpio_num_t trig_pin,
                                   gpio_num_t echo_pin,
                                   const UltrasonicConfig &cfg)
    : cfg_(cfg)
    , trig_pin_(trig_pin)
    , echo_pin_(echo_pin)
{
    if (cfg_.ping_count == 0) {
        ESP_LOGW(TAG, "ping_count cannot be zero. Setting to 1.");
        cfg_.ping_count = 1;
    }
    if (cfg_.ping_count > MAX_PINGS) {
        ESP_LOGW(TAG, "ping_count=%u exceeds MAX_PINGS=%u. Capping value.", cfg_.ping_count,
                 (unsigned)MAX_PINGS);
        cfg_.ping_count = MAX_PINGS;
    }
}

bool UltrasonicSensor::init()
{
    ESP_LOGD(TAG, "Initializing GPIO interface for UltrasonicSensor");

    // Configure TRIG pin as output
    gpio_reset_pin(trig_pin_);
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << trig_pin_,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&io_conf) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure TRIG pin");
        return false;
    }
    ESP_LOGI(TAG, "TRIG pin configured");
    gpio_set_level(trig_pin_, 0);

    // Configure ECHO pin as input
    gpio_reset_pin(echo_pin_);
    io_conf = {
        .pin_bit_mask = 1ULL << echo_pin_,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&io_conf) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ECHO pin");
        return false;
    }
    ESP_LOGI(TAG, "ECHO pin configured");

    return true;
}

void UltrasonicSensor::setPingCount(uint8_t new_ping_count)
{
    if (new_ping_count == 0) {
        ESP_LOGW(TAG, "ping_count cannot be zero. Setting to 1.");
        cfg_.ping_count = 1;
    }
    else if (new_ping_count > MAX_PINGS) {
        ESP_LOGW(TAG, "ping_count=%u exceeds MAX_PINGS=%u. Capping value.", new_ping_count,
                 (unsigned)MAX_PINGS);
        cfg_.ping_count = MAX_PINGS;
    }
    else {
        cfg_.ping_count = new_ping_count;
    }
}

bool UltrasonicSensor::read_raw_cm_(float &cm, UsFailure &out_failure)
{
    out_failure = UsFailure::NONE;

    // Send trigger pulse
    gpio_set_level(trig_pin_, 1);
    esp_rom_delay_us(cfg_.ping_duration_us);
    gpio_set_level(trig_pin_, 0);

    // --- Measure pulse ---
    // The entire measurement process must complete within the timeout period.
    uint64_t start_time = esp_timer_get_time();
    uint64_t echo_start = 0;
    uint64_t echo_end   = 0;

    // 1. Wait for the echo pin to go high (start of pulse)
    while (gpio_get_level(echo_pin_) == 0) {
        if (esp_timer_get_time() - start_time > cfg_.timeout_us) {
            ESP_LOGD(TAG, "Timeout waiting for ECHO pulse start");
            out_failure = UsFailure::TIMEOUT;
            return false;
        }
    }
    echo_start = esp_timer_get_time();

    // 2. Wait for the echo pin to go low (end of pulse)
    while (gpio_get_level(echo_pin_) == 1) {
        if (esp_timer_get_time() - start_time > cfg_.timeout_us) {
            ESP_LOGD(TAG, "Timeout waiting for ECHO pulse end");
            out_failure = UsFailure::TIMEOUT;
            return false;
        }
    }
    echo_end = esp_timer_get_time();

    // --- Calculate distance ---
    if (echo_start == 0 || echo_end == 0) {
        out_failure = UsFailure::HW_ERROR;
        return false;
    }

    uint32_t pulse_duration_us = echo_end - echo_start;
    cm                         = (pulse_duration_us * SOUND_SPEED_CM_PER_US) / 2.0f;

    return true;
}

float UltrasonicSensor::reduce_median(float *v, size_t n)
{
    std::sort(v, v + n);
    return v[n / 2];
}

float UltrasonicSensor::reduce_dominant_cluster(float *v, size_t n)
{
    static constexpr float DELTA_CM =
        5.0f; // Maximum distance between values in same cluster

    std::sort(v, v + n);

    float best      = v[0];
    size_t best_cnt = 1;
    float cur       = v[0];
    size_t cur_cnt  = 1;

    // Find the largest cluster of values within DELTA_CM of each other
    for (size_t i = 1; i < n; i++) {
        if (fabsf(v[i] - cur) <= DELTA_CM) {
            cur_cnt++;
        }
        else {
            if (cur_cnt > best_cnt) {
                best     = cur;
                best_cnt = cur_cnt;
            }
            cur     = v[i];
            cur_cnt = 1;
        }
    }

    // Check if last cluster is the largest
    if (cur_cnt > best_cnt)
        best = cur;

    return best;
}

bool UltrasonicSensor::read_distance_cm(float &out_cm,
                                        UsQuality &out_quality,
                                        UsFailure &out_failure)
{
    out_quality = UsQuality::INVALID;
    out_failure = UsFailure::NONE;

    size_t timeout_cnt = 0;
    size_t hw_err_cnt  = 0;

    // Optional blind ping to clear environment
    if (cfg_.blind_ping) {
        float d;
        UsFailure f;
        read_raw_cm_(d, f);
        vTaskDelay(pdMS_TO_TICKS(cfg_.ping_interval_ms));
    }

    // Collect multiple samples
    float samples[MAX_PINGS];
    size_t valid = 0;

    for (size_t i = 0; i < cfg_.ping_count; i++) {
        float d;
        UsFailure f;

        if (read_raw_cm_(d, f)) {
            samples[valid++] = d;
            ESP_LOGD(TAG, "d=%.2f cm failure=%d", d, (int)f);
        }
        else {
            // Track failure types
            if (f == UsFailure::TIMEOUT)
                timeout_cnt++;
            else if (f == UsFailure::HW_ERROR)
                hw_err_cnt++;
            ESP_LOGD(TAG, "ping failure=%d", (int)f);
        }

        // Wait between pings (except after last one)
        if (i + 1 < cfg_.ping_count)
            vTaskDelay(pdMS_TO_TICKS(cfg_.ping_interval_ms));
    }

    // Process collected samples
    if (valid == 0) {
        out_quality = UsQuality::INVALID;
        out_failure = (hw_err_cnt > 0) ? UsFailure::HW_ERROR : UsFailure::TIMEOUT;
        return false;
    }

    // Determine quality based on valid ping ratio
    float ratio = (float)valid / cfg_.ping_count;
    out_quality = (ratio >= US_VALID_PING_RATIO) ? UsQuality::OK : UsQuality::WEAK;

    // Apply selected filter
    float value;
    if (cfg_.filter == Filter::MEDIAN) {
        value = reduce_median(samples, valid);
    }
    else {
        value = reduce_dominant_cluster(samples, valid);
    }

    ESP_LOGD(TAG, "valid=%u ratio=%.2f value=%.2f", valid, ratio, value);

    out_cm = value;
    return true;
}