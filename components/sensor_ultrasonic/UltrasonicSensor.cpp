#include "UltrasonicSensor.hpp"
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "esp_log.h"
#include <algorithm>
#include <cmath>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "UltrasonicSensor";

float UltrasonicSensor::reduce_median(float *v, size_t n)
{
    std::sort(v, v + n);
    return v[n / 2];
}

float UltrasonicSensor::reduce_dominant_cluster(float *v, size_t n)
{
    static constexpr float DELTA_CM = 5.0f;

    std::sort(v, v + n);

    float  best     = v[0];
    size_t best_cnt = 1;

    float  cur     = v[0];
    size_t cur_cnt = 1;

    for (size_t i = 1; i < n; i++)
    {
        if (fabsf(v[i] - cur) <= DELTA_CM)
        {
            cur_cnt++;
        }
        else
        {
            if (cur_cnt > best_cnt)
            {
                best     = cur;
                best_cnt = cur_cnt;
            }
            cur     = v[i];
            cur_cnt = 1;
        }
    }

    if (cur_cnt > best_cnt)
        best = cur;

    return best;
}

bool UltrasonicSensor::readDistanceCm(float &out_cm, UsQuality &out_quality, UsFailure &out_failure)
{
    out_quality = UsQuality::INVALID;
    out_failure = UsFailure::NONE;

    size_t timeout_cnt = 0;
    size_t hw_err_cnt  = 0;

    if (cfg.blind_ping)
    {
        float     d;
        UsFailure f;
        readRawDistanceCm(d, f);
        vTaskDelay(pdMS_TO_TICKS(cfg.ping_interval_ms));
    }

    float  samples[cfg.ping_count];
    size_t valid = 0;

    for (size_t i = 0; i < cfg.ping_count; i++)
    {
        float     d;
        UsFailure f;

        if (readRawDistanceCm(d, f))
        {
            samples[valid++] = d;
            ESP_LOGD(TAG, "d=%.2f cm failure=%d", d, (int)f);
        }
        else
        {
            if (f == UsFailure::TIMEOUT)
                timeout_cnt++;
            else if (f == UsFailure::HW_ERROR)
                hw_err_cnt++;
            ESP_LOGD(TAG, "ping failure=%d", (int)f);
        }

        if (i + 1 < cfg.ping_count)
            vTaskDelay(pdMS_TO_TICKS(cfg.ping_interval_ms));
    }

    // --- pós-coleta ---

    if (valid == 0)
    {
        out_quality = UsQuality::INVALID;
        out_failure = (hw_err_cnt > 0) ? UsFailure::HW_ERROR : UsFailure::TIMEOUT;
        return false;
    }

    float ratio = (float)valid / cfg.ping_count;

    out_quality = (ratio >= US_VALID_PING_RATIO) ? UsQuality::OK : UsQuality::WEAK;

    float median  = reduce_median(samples, valid);
    float cluster = reduce_dominant_cluster(samples, valid);

    ESP_LOGD(TAG, "valid=%u ratio=%.2f median=%.2f cluster=%.2f", valid, ratio, median, cluster);

    out_cm = (cfg.filter == Filter::MEDIAN) ? median : cluster;
    return true;
}
