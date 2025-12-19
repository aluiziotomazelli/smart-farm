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

bool UltrasonicSensor::readDistanceCm(float &out_cm)
{
    if (cfg.blind_ping)
    {
        readRawDistanceCm();
        vTaskDelay(pdMS_TO_TICKS(cfg.ping_interval_ms));
    }

    float  samples[cfg.ping_count];
    size_t valid = 0;

    for (size_t i = 0; i < cfg.ping_count; i++)
    {
        float d = readRawDistanceCm();
        ESP_LOGD(TAG, "raw_d:%.2f cm", d);
        if (d > 0)
            samples[valid++] = d;

        if (i + 1 < cfg.ping_count)
            vTaskDelay(pdMS_TO_TICKS(cfg.ping_interval_ms));
    }

    if (valid == 0)
        return false;
    else
    {
        // #if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
        //         for (size_t i = 0; i < valid; i++)
        //         {
        //             ESP_LOGD(TAG, "sample[%u]=%.2f cm", i, samples[i]);
        //         }
        // #endif
    }

    float median  = reduce_median(samples, valid);
    float cluster = reduce_dominant_cluster(samples, valid);

    ESP_LOGD(TAG, "valid=%u median=%.2f cluster=%.2f", valid, median, cluster);

    out_cm = (cfg.filter == Filter::MEDIAN) ? median : cluster;
    return true;
}
