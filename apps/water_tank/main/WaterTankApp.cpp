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
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    ESP_LOGI(TAG, "Wakeup cause: %d", cause);
    if (cause == ESP_SLEEP_WAKEUP_EXT0)
        ESP_LOGI(TAG, "Wakeup cause: ESP_SLEEP_WAKEUP_EXT0");

    ESP_LOGI(TAG, "Initializing WaterTankApp");

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
    ESP_LOGI(TAG, "First run");
    while (true)
    {
        // vTaskDelay(pdMS_TO_TICKS(2000));
        ESP_LOGI(TAG, "Going to deep sleep in 3 seconds...");
        vTaskDelay(pdMS_TO_TICKS(3000));

        if (floatswitch.read() == true)
        {
            ESP_LOGI(TAG, "Boia ativa, não dormir");
        }
        else
        {
            ESP_LOGI(TAG, "Boia inativa, dormindo...");
            esp_deep_sleep_start();
        }
    }
}
