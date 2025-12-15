#include "HCSR04Gpio.hpp"
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG // Must come before esp_log.h
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

static const char *TAG = "HCSR04_GPIO";

// UltrasonicConfig cfg;

HCSR04Gpio::HCSR04Gpio(gpio_num_t                                trig,
                       gpio_num_t                                echo,
                       const UltrasonicSensor::UltrasonicConfig &cfg)
    : UltrasonicSensor{cfg}
    , trig_pin(trig)
    , echo_pin(echo)
{
}

bool HCSR04Gpio::init()
{
    ESP_LOGI(TAG, "Initializing HCSR04Gpio sensor (trig=%d, echo=%d)", trig_pin, echo_pin);

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

    gpio_reset_pin(echo_pin);

    io_conf = {
        .pin_bit_mask = 1ULL << echo_pin,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    if (gpio_config(&io_conf) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure ECHO pin");
        return false;
    }
    else
    {
        ESP_LOGI(TAG, "ECHO pin configured");
    }

    gpio_set_level(trig_pin, 0);
    return true;
}

float HCSR04Gpio::readRawDistanceCm()
{
    gpio_set_level(trig_pin, 1);
    esp_rom_delay_us(cfg.ping_duration_us);
    gpio_set_level(trig_pin, 0);

    int64_t start = esp_timer_get_time();
    while (gpio_get_level(echo_pin) == 0)
    {
        if (esp_timer_get_time() - start > cfg.timeout_us)
            return -1;
        esp_rom_delay_us(1);
    }

    int64_t echo_start = esp_timer_get_time();
    while (gpio_get_level(echo_pin) == 1)
    {
        if (esp_timer_get_time() - echo_start > cfg.timeout_us)
            return -1;
        esp_rom_delay_us(1);
    }

    int64_t pulse = esp_timer_get_time() - echo_start;
    return pulse * 0.0343f / 2.0f;
}
