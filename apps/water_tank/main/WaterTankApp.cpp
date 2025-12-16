#include "WaterTankApp.hpp"
#include "FloatSwitch.hpp"
#include "HCSR04Gpio.hpp"
#include "HCSR04Rmt.hpp"
#define LOG_LOCAL_LEVEL ESP_LOG_INFO // Must be call before esp_log.h
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

const char *TAG = "WaterTankApp";

static FloatSwitch::Config fs_cfg = {.pin           = GPIO_NUM_4, // exemplo
                                     .normally_open = true,
                                     .pull          = FloatSwitch::Pull::UP,
                                     .debounce_ms   = 50,
                                     .wakeup_edge   = FloatSwitch::WakeupLevel::LOW};

static FloatSwitch floatswitch(fs_cfg);

void WaterTankApp::init()
{
    ESP_LOGI(TAG, "Initializing WaterTankApp");

    floatswitch.init();

    vTaskDelay(pdMS_TO_TICKS(100));
}

void WaterTankApp::run()
{
    ESP_LOGI(TAG, "First run");
    while (true)
    {
        FloatSwitch::WakeupInfo wi;
        if (floatswitch.get_wakeup_info(wi))
        {
            ESP_LOGI(TAG, "Config wakeup: pin=%d level=%d", wi.pin, wi.level);
            esp_sleep_enable_ext0_wakeup(wi.pin, wi.level);
        }

        // vTaskDelay(pdMS_TO_TICKS(2000));
        ESP_LOGI(TAG, "Going to deep sleep in 3 seconds...");
        vTaskDelay(pdMS_TO_TICKS(3000));

        ESP_LOGI(TAG, "Going to deep sleep now...");
        esp_deep_sleep_start();
    }
}
