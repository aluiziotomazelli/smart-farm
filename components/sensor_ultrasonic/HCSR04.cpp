#include "HCSR04.hpp"
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "HCSR04";

HCSR04::HCSR04(gpio_num_t trig, const UltrasonicSensor::UltrasonicConfig &cfg)
    : UltrasonicSensor{cfg}
    , trig_pin(trig)
{
}

bool HCSR04::init()
{
    ESP_LOGI(TAG, "Initializing HCSR04 sensor (trig=%d)", trig_pin);

    gpio_reset_pin(trig_pin);

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << trig_pin,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    if (gpio_config(&io_conf) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure TRIG pin");
        return false;
    }
    else
    {
        ESP_LOGI(TAG, "TRIG pin configured");
    }

    gpio_set_level(trig_pin, 0);
    return true;
}

void HCSR04::send_ping()
{
    gpio_set_level(trig_pin, 1);
    esp_rom_delay_us(cfg.ping_duration_us);
    gpio_set_level(trig_pin, 0);
}
