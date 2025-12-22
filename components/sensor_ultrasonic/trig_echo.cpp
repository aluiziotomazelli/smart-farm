#include "trig_echo.hpp"
#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "TrigerEcho";

TrigerEcho::TrigerEcho(gpio_num_t trig, const UltrasonicSensor::UltrasonicConfig &cfg)
    : UltrasonicSensor{cfg}
    , trig_pin_(trig)
{
}

bool TrigerEcho::init()
{
    ESP_LOGI(TAG, "Initializing TrigerEcho sensor (trig=%d)", trig_pin_);

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
    else {
        ESP_LOGI(TAG, "TRIG pin configured");
    }

    gpio_set_level(trig_pin_, 0);
    return true;
}

void TrigerEcho::send_ping()
{
    gpio_set_level(trig_pin_, 1);
    esp_rom_delay_us(cfg.ping_duration_us);
    gpio_set_level(trig_pin_, 0);
}
