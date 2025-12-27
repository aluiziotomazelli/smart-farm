#include "ultrasonic_sensor.hpp"
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "esp_log.h"
#include "esp_timer.h"
#include <algorithm>
#include <cmath>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "UltrasonicSensor";

const char* us_failure_to_string(UsFailure failure)
{
    switch (failure) {
        case UsFailure::NONE:
            return "No failure";
        case UsFailure::TIMEOUT:
            return "Echo timeout";
        case UsFailure::HW_ERROR:
            return "Hardware error (pin stuck)";
        case UsFailure::INVALID_PULSE:
            return "Invalid pulse width";
        case UsFailure::HIGH_VARIANCE:
            return "High variance in readings";
        default:
            return "Unknown failure";
    }
}

UltrasonicSensor::UltrasonicSensor(gpio_num_t trig_pin,
                                   gpio_num_t echo_pin,
                                   const UltrasonicConfig &cfg)
    : cfg_(cfg)
    , trig_pin_(trig_pin)
    , echo_pin_(echo_pin)
{
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
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&io_conf) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ECHO pin");
        return false;
    }
    ESP_LOGI(TAG, "ECHO pin configured");

    return true;
}

bool UltrasonicSensor::readRaw_cm_(float &cm, UsFailure &out_failure)
{
    // Check initial state of the ECHO pin. If it's already high, it might be stuck.
    if (gpio_get_level(echo_pin_) == 1) {
        ESP_LOGE(TAG, "ECHO pin stuck HIGH");
        out_failure = UsFailure::HW_ERROR;
        return false;
    }

    // Send trigger pulse
    gpio_set_level(trig_pin_, 1);
    esp_rom_delay_us(cfg_.ping_duration_us);
    gpio_set_level(trig_pin_, 0);

    // Wait for echo pulse to start (rising edge)
    uint32_t start_time = esp_timer_get_time();
    while (gpio_get_level(echo_pin_) == 0) {
        if (esp_timer_get_time() - start_time > cfg_.timeout_us) {
            ESP_LOGW(TAG, "GPIO: Timeout waiting for ECHO pulse start");
            out_failure = UsFailure::TIMEOUT;
            return false;
        }
    }

    // Measure the duration of the echo pulse (high level)
    uint32_t echo_start = esp_timer_get_time();
    while (gpio_get_level(echo_pin_) == 1) {
        if (esp_timer_get_time() - echo_start > cfg_.timeout_us) {
            ESP_LOGW(TAG, "GPIO: Timeout waiting for ECHO pulse end");
            out_failure = UsFailure::TIMEOUT;
            return false;
        }
    }
    uint32_t echo_end = esp_timer_get_time();

    // Calculate distance: (time × speed of sound) / 2
    uint32_t pulse_duration_us = echo_end - echo_start;
    cm                         = (pulse_duration_us * SOUND_SPEED_CM_PER_US) / 2.0f;

    // Check if the calculated distance is within the valid range.
    if (cm < cfg_.min_distance_cm || cm > cfg_.max_distance_cm) {
        ESP_LOGW(TAG, "Invalid distance: %.2f cm", cm);
        out_failure = UsFailure::INVALID_PULSE;
        return false;
    }

    out_failure = UsFailure::NONE;
    return true;
}

float UltrasonicSensor::reduceMedian(float *v, size_t n)
{
    std::sort(v, v + n);
    return v[n / 2];
}

float UltrasonicSensor::reduceDominantCluster(float *v, size_t n)
{
    static constexpr float DELTA_CM = 5.0f; // Max distance between values in the same cluster

    std::sort(v, v + n);

    float best_cluster_val = v[0];
    size_t best_cluster_size = 1;
    float current_cluster_val = v[0];
    size_t current_cluster_size = 1;

    // Find the largest cluster of values within DELTA_CM of each other
    for (size_t i = 1; i < n; i++) {
        if (fabsf(v[i] - current_cluster_val) <= DELTA_CM) {
            current_cluster_size++;
        }
        else {
            if (current_cluster_size > best_cluster_size) {
                best_cluster_val = current_cluster_val;
                best_cluster_size = current_cluster_size;
            }
            current_cluster_val = v[i];
            current_cluster_size = 1;
        }
    }

    // Check if the last cluster was the largest
    if (current_cluster_size > best_cluster_size) {
        best_cluster_val = current_cluster_val;
    }

    return best_cluster_val;
}

bool UltrasonicSensor::readDistance_cm(float &out_cm,
                                       UsQuality &out_quality,
                                       UsFailure &out_failure)
{
    out_quality = UsQuality::INVALID;
    out_failure = UsFailure::NONE;

    uint8_t timeout_cnt = 0;
    uint8_t hw_err_cnt  = 0;
    uint8_t invalid_cnt = 0;

    // Optional blind ping to clear the environment before taking real samples.
    if (cfg_.blind_ping) {
        float dummy_distance;
        UsFailure dummy_failure;
        readRaw_cm_(dummy_distance, dummy_failure);
        vTaskDelay(pdMS_TO_TICKS(cfg_.ping_interval_ms));
    }

    // Collect multiple samples
    float samples[MAX_PINGS];
    const size_t ping_count = std::min((size_t)cfg_.ping_count, MAX_PINGS);
    size_t valid = 0;

    for (size_t i = 0; i < ping_count; i++) {
        float d;
        UsFailure f;
        if (readRaw_cm_(d, f)) {
            samples[valid++] = d;
            ESP_LOGD(TAG, "Ping success: d=%.2f cm", d);
        }
        else {
            // Track failure types for later analysis
            if (f == UsFailure::TIMEOUT) timeout_cnt++;
            else if (f == UsFailure::HW_ERROR) hw_err_cnt++;
            else if (f == UsFailure::INVALID_PULSE) invalid_cnt++;
            ESP_LOGD(TAG, "Ping failure: %s", us_failure_to_string(f));
        }

        // Wait between pings (except after the last one)
        if (i + 1 < ping_count) {
            vTaskDelay(pdMS_TO_TICKS(cfg_.ping_interval_ms));
        }
    }

    // --- Process collected samples ---

    // 1. Check if any valid samples were collected
    float ratio = (ping_count > 0) ? ((float)valid / ping_count) : 0;
    if (ratio < US_INVALID_PING_RATIO) {
        out_quality = UsQuality::INVALID;
        // Determine the most likely cause of failure
        if (hw_err_cnt > 0) out_failure = UsFailure::HW_ERROR;
        else if (timeout_cnt > invalid_cnt) out_failure = UsFailure::TIMEOUT;
        else out_failure = UsFailure::INVALID_PULSE;
        ESP_LOGW(TAG, "Invalid reading: valid ping ratio too low (%.2f)", ratio);
        return false;
    }

    // 2. Calculate mean and standard deviation for valid samples
    float mean = 0;
    for (size_t i = 0; i < valid; i++) mean += samples[i];
    mean /= valid;

    float variance = 0;
    for (size_t i = 0; i < valid; i++) variance += pow(samples[i] - mean, 2);
    float std_dev = sqrtf(variance / valid);
    ESP_LOGD(TAG, "Samples: %u/%u, Mean: %.2f, StdDev: %.2f", valid, ping_count, mean, std_dev);

    // 3. Check for high variance (erratic readings)
    if (std_dev > cfg_.max_dev_cm) {
        ESP_LOGW(TAG, "High variance detected: std_dev=%.2f cm (max=%.2f)", std_dev, cfg_.max_dev_cm);
        out_quality = UsQuality::INVALID;
        out_failure = UsFailure::HIGH_VARIANCE;
        return false;
    }

    // 4. Determine final quality based on ping ratio
    if (ratio >= US_VALID_PING_RATIO) {
        out_quality = UsQuality::OK;
        // Downgrade to WEAK if variance is high but still acceptable
        if (std_dev > cfg_.max_dev_cm * 0.6f) {
            out_quality = UsQuality::WEAK;
        }
    } else { // Ratio is between INVALID and VALID thresholds
        out_quality = UsQuality::WEAK;
        // Per requirements, if the signal is already weak due to low ping count,
        // a moderately high variance should invalidate it completely.
        if (std_dev > cfg_.max_dev_cm * 0.6f) {
            ESP_LOGW(TAG, "Weak signal with high variance, invalidating. std_dev=%.2f", std_dev);
            out_quality = UsQuality::INVALID;
            out_failure = UsFailure::HIGH_VARIANCE;
            return false;
        }
    }

    // 5. Apply the selected filter to get the final value
    float value;
    if (cfg_.filter == Filter::MEDIAN) {
        value = reduceMedian(samples, valid);
    } else {
        value = reduceDominantCluster(samples, valid);
    }

    ESP_LOGD(TAG, "Final result: value=%.2f cm, quality=%d", value, (int)out_quality);
    out_cm = value;
    return true;
}