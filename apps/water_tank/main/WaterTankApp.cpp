#include "WaterTankApp.hpp"
#include "HCSR04Gpio.hpp"
#include "HCSR04Rmt.hpp"
#include "config.hpp"
#define LOG_LOCAL_LEVEL ESP_LOG_INFO // Must be call before esp_log.h
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

const char *TAG = "WaterTankApp";

static const UltrasonicSensor::UltrasonicConfig cfg = {
    .ping_count       = 7,
    .ping_interval_ms = 70,
    .ping_duration_us = 20,
    .timeout_us       = 25000,
    .filter           = UltrasonicSensor::Filter::DOMINANT_CLUSTER,
    .blind_ping       = true};

// HCSR04Gpio sensor(GPIO_NUM_21, GPIO_NUM_19, cfg);
HCSR04Rmt sensor(GPIO_NUM_21, GPIO_NUM_19, cfg);

static float distance_to_percent(float d_cm)
{
    if (d_cm <= TANK_DISTANCE_FULL_CM)
        return 100.0f;
    if (d_cm >= TANK_DISTANCE_EMPTY_CM)
        return 0.0f;

    return 100.0f * (TANK_DISTANCE_EMPTY_CM - d_cm) /
           (TANK_DISTANCE_EMPTY_CM - TANK_DISTANCE_FULL_CM);
}

void WaterTankApp::init()
{
    ESP_LOGI(TAG, "Initializing WaterTankApp");
    sensor.init();
    vTaskDelay(pdMS_TO_TICKS(100));
}

void WaterTankApp::run()
{
    ESP_LOGI(TAG, "First run");
    while (true)
    {
        float distance;
        if (!sensor.readDistanceCm(distance))
        {
            ESP_LOGW(TAG, "sensor error");
        }
        else
        {
            float level = distance_to_percent(distance);
            ESP_LOGI(TAG, "distance=%.1f cm level=%.1f%%", distance, level);
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
