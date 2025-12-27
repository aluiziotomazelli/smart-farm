#include "ultrasonic_sensor.hpp"
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
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
    // 1. Teste de estado inicial dos pinos
    int echo_initial = gpio_get_level(echo_pin_);
    if (echo_initial == 1) {
        ESP_LOGE(TAG, "ECHO pin stuck HIGH");
        out_failure = UsFailure::HW_ERROR;
        return false;
    }

    // Send trigger pulse
    gpio_set_level(trig_pin_, 1);
    esp_rom_delay_us(cfg_.ping_duration_us);
    gpio_set_level(trig_pin_, 0);

    // Wait for echo start (rising edge)
    uint32_t start_time = esp_timer_get_time();
    while (gpio_get_level(echo_pin_) == 0) {
        if (esp_timer_get_time() - start_time > cfg_.timeout_us) {
            ESP_LOGW(TAG, "GPIO: Timeout waiting for ECHO pulse start");
            out_failure = UsFailure::TIMEOUT;
            return false;
        }
    }

    // Measure echo pulse width (high duration)
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

bool UltrasonicSensor::readDistance_cm(float &out_cm,
                                       UsQuality &out_quality,
                                       UsFailure &out_failure)
{
    out_quality = UsQuality::INVALID;
    out_failure = UsFailure::NONE;

    uint8_t timeout_cnt = 0;
    uint8_t hw_err_cnt  = 0;
    uint8_t invalid_cnt = 0;

    // Optional blind ping to clear environment
    if (cfg_.blind_ping) {
        float d;
        UsFailure f;
        readRaw_cm_(d, f);
        vTaskDelay(pdMS_TO_TICKS(cfg_.ping_interval_ms));
    }

    // Collect multiple samples
    float samples[MAX_PINGS];
    const size_t ping_count =
        (cfg_.ping_count <= MAX_PINGS) ? cfg_.ping_count : MAX_PINGS;
    size_t valid = 0;

    for (size_t i = 0; i < ping_count; i++) {
        float d;
        UsFailure f;

        if (readRaw_cm_(d, f)) {
            samples[valid++] = d;
            ESP_LOGD(TAG, "d=%.2f cm failure=%d", d, (int)f);
        }
        else {
            // Track failure types
            if (f == UsFailure::TIMEOUT)
                timeout_cnt++;
            else if (f == UsFailure::HW_ERROR)
                hw_err_cnt++;
            else if (f == UsFailure::INVALID_PULSE)
                invalid_cnt++;

            ESP_LOGD(TAG, "ping failure=%d", (int)f);
        }

        // Wait between pings (except after last one)
        if (i + 1 < ping_count)
            vTaskDelay(pdMS_TO_TICKS(cfg_.ping_interval_ms));
    }

    // Process collected samples
    if (valid == 0) {
        out_quality = UsQuality::INVALID;

        if (invalid_cnt > hw_err_cnt && invalid_cnt > timeout_cnt)
            out_failure = UsFailure::INVALID_PULSE;
        else
            out_failure = (hw_err_cnt > 0) ? UsFailure::HW_ERROR : UsFailure::TIMEOUT;

        return false;
    }

    // Calcula média
    float mean = 0;
    for (size_t i = 0; i < valid; i++) {
        mean += samples[i];
    }
    mean /= valid;

    // Calcula desvio padrão
    float variance = 0;
    for (size_t i = 0; i < valid; i++) {
        float diff = samples[i] - mean;
        variance += diff * diff;
    }
    float std_dev = sqrtf(variance / valid);

    ESP_LOGD(TAG, "mean=%.2f std_dev=%.2f", mean, std_dev);

    // Se desvio padrão muito alto, indica leituras erráticas
    if (std_dev > cfg_.max_dev_cm) {
        ESP_LOGW(TAG, "High variance detected: std_dev=%.2f cm (max=%.2f)!", std_dev,
                 cfg_.max_dev_cm);
        out_quality = UsQuality::INVALID;
        out_failure = UsFailure::HIGH_VARIANCE;
        return false;
    }

    // Determine quality based on valid ping ratio
    float ratio = (float)valid / cfg_.ping_count;

    if (ratio >= US_VALID_PING_RATIO) {
        // Se desvio alto mas ainda aceitável, reduz qualidade
        if (std_dev > cfg_.max_dev_cm * 0.6f) {
            out_quality = UsQuality::WEAK;
        }
        else {
            out_quality = UsQuality::OK;
        }
    }
    else {
        out_quality = UsQuality::WEAK;
    }

    // Apply selected filter
    float value;
    if (cfg_.filter == Filter::MEDIAN) {
        value = reduceMedian(samples, valid);
    }
    else {
        value = reduceDominantCluster(samples, valid);
    }

    ESP_LOGD(TAG, "valid=%u ratio=%.2f value=%.2f", valid, ratio, value);

    out_cm = value;
    return true;
}