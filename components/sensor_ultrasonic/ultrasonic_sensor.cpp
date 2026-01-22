#include "ultrasonic_sensor.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "esp_log.h"
#include "esp_timer.h"
#include <algorithm>
#include <cmath>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "UltrasonicSensor";

const char *us_failure_to_string(UsFailure failure)
{
    switch (failure) {
    case UsFailure::NONE:
        return "No failure";
    case UsFailure::TIMEOUT:
        return "Echo timeout";
    case UsFailure::HW_ERROR:
        return "Hardware error";
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
                                   UltrasonicConfig &cfg)
    : cfg_(cfg)
    , trig_pin_(trig_pin)
    , echo_pin_(echo_pin)
{
    if (cfg_.ping_count == 0) {
        ESP_LOGW(TAG, "ping_count cannot be zero. Setting to 1.");
        cfg_.ping_count = 1;
    }
    if (cfg_.ping_count > MAX_PINGS) {
        ESP_LOGW(TAG, "ping_count=%u exceeds MAX_PINGS=%u. Capping value.",
                 cfg_.ping_count, (unsigned)MAX_PINGS);
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
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&io_conf) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ECHO pin");
        return false;
    }

    // Configure ECHO pin as output, set it to low, pull it to ground
    // and clear sensor noise from any residual interference
    gpio_set_direction(echo_pin_, GPIO_MODE_OUTPUT);
    gpio_set_level(echo_pin_, 0);

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
    if (cm > cfg_.max_distance_cm || cm < cfg_.min_distance_cm) {
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
    static constexpr float DELTA_CM = 5.0f;
    std::sort(v, v + n);

    float best_cluster_sum   = 0;
    size_t best_cluster_size = 0;

    for (size_t i = 0; i < n; i++) {
        float current_sum   = 0;
        size_t current_size = 0;

        // Attempt to find a group of values that are within DELTA_CM of v[i]
        for (size_t j = i; j < n; j++) {
            if (fabsf(v[j] - v[i]) <= DELTA_CM) {
                current_sum += v[j];
                current_size++;
            }
            else {
                break; // How is ordered, can break early
            }
        }

        // If this cluster is larger than the previous best, it becomes the new
        // dominant
        if (current_size > best_cluster_size) {
            best_cluster_size = current_size;
            best_cluster_sum  = current_sum;
        }
    }
    float average =
        (best_cluster_size > 0) ? (best_cluster_sum / best_cluster_size) : 0.0f;
    ESP_LOGD(TAG, "Dominant cluster: size=%u, average=%.2f", best_cluster_size,
             average);

    return average;
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

    // Configure echo pin as output and set it to low to pull it to ground
    // and clear sensor noise from any residual interference
    // It will be set as input before reading the distance
    gpio_set_direction(echo_pin_, GPIO_MODE_OUTPUT);
    gpio_set_level(echo_pin_, 0);

    // Wait for the sensor to warm up
    vTaskDelay(pdMS_TO_TICKS(cfg_.warmup_time_ms));

    // Configure echo pin as input

    // Optional blind ping to clear the environment before taking real samples.
    if (cfg_.blind_ping) {
        float dummy_distance;
        UsFailure dummy_failure;
        readRaw_cm_(dummy_distance, dummy_failure);
        vTaskDelay(pdMS_TO_TICKS(cfg_.ping_interval_ms));
    }

    // Collect multiple samples
    float samples[MAX_PINGS];
    uint8_t valid = 0;

    for (size_t i = 0; i < cfg_.ping_count; i++) {
        gpio_set_direction(echo_pin_, GPIO_MODE_INPUT);
        float distance;
        UsFailure failure;
        if (readRaw_cm_(distance, failure)) {
            samples[valid++] = distance;
            ESP_LOGD(TAG, "Ping success: d=%.2f cm", distance);
        }
        else {
            // Track failure types for later analysis
            if (failure == UsFailure::TIMEOUT)
                timeout_cnt++;
            else if (failure == UsFailure::HW_ERROR)
                hw_err_cnt++;
            else if (failure == UsFailure::INVALID_PULSE)
                invalid_cnt++;
            ESP_LOGD(TAG, "Ping failure: %s", us_failure_to_string(failure));
        }

        gpio_set_direction(echo_pin_, GPIO_MODE_OUTPUT);
        gpio_set_level(echo_pin_, 0);
        // Wait between pings (except after the last one)
        if (i + 1 < cfg_.ping_count) {
            vTaskDelay(pdMS_TO_TICKS(cfg_.ping_interval_ms));
        }
    }

    // --- Process collected samples ---

    // 1. Check if any valid samples were collected
    float ratio = (cfg_.ping_count > 0) ? ((float)valid / cfg_.ping_count) : 0;
    if (ratio < US_INVALID_PING_RATIO) {
        out_quality = UsQuality::INVALID;
        // Determine the most likely cause of failure
        if (hw_err_cnt > 0)
            out_failure = UsFailure::HW_ERROR;
        else if (timeout_cnt > invalid_cnt)
            out_failure = UsFailure::TIMEOUT;
        else
            out_failure = UsFailure::INVALID_PULSE;
        ESP_LOGW(TAG, "Invalid reading: valid ping ratio too low (%.2f)", ratio);
        return false;
    }

    // 2. Calculate mean and standard deviation for valid samples
    float std_dev = getStdDev(samples, valid);

    // 3. Check for high variance (erratic readings)
    if (std_dev > cfg_.max_dev_cm) {
        ESP_LOGW(TAG, "High variance detected: std_dev=%.2f cm (max=%.2f)", std_dev,
                 cfg_.max_dev_cm);
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
            out_failure = UsFailure::HIGH_VARIANCE;
        }
    }
    else { // Ratio is between INVALID and VALID thresholds
        out_quality = UsQuality::WEAK;
        // Per requirements, if the signal is already weak due to low ping count,
        // a moderately high variance should invalidate it completely.
        if (std_dev > cfg_.max_dev_cm * 0.6f) {
            ESP_LOGW(
                TAG,
                "Weak signal with high variance, invalidating, std_dev = % .2f ",
                std_dev);
            out_quality = UsQuality::INVALID;
            out_failure = UsFailure::HIGH_VARIANCE;
            return false;
        }
    }

    // 5. Apply the selected filter to get the final value
    float value;
    if (cfg_.filter == Filter::MEDIAN) {
        value = reduceMedian(samples, valid);
    }
    else {
        value = reduceDominantCluster(samples, valid);
    }

    ESP_LOGD(TAG, "Final result: value=%.2f cm, quality=%d", value,
             (int)out_quality);
    out_cm = value;
    return true;
}

float UltrasonicSensor::getStdDev(float *samples, uint8_t valids)
{
    float mean = 0;
    for (uint8_t i = 0; i < valids; i++) mean += samples[i];
    mean /= valids;

    float variance = 0;
    for (size_t i = 0; i < valids; i++) variance += pow(samples[i] - mean, 2);
    float std_dev = sqrtf(variance / valids);
    ESP_LOGD(TAG, "Samples: %u, Mean: %.2f, StdDev: %.2f", valids, mean, std_dev);

    return std_dev;
}

void UltrasonicSensor::setPingCount(uint8_t new_ping_count)
{
    if (new_ping_count == 0) {
        ESP_LOGW(TAG, "ping_count cannot be zero. Setting to 1.");
        cfg_.ping_count = 1;
    }
    else if (new_ping_count > MAX_PINGS) {
        ESP_LOGW(TAG, "ping_count=%u exceeds MAX_PINGS=%u. Capping value.",
                 new_ping_count, (unsigned)MAX_PINGS);
        cfg_.ping_count = MAX_PINGS;
    }
    else {
        cfg_.ping_count = new_ping_count;
    }
}