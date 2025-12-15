#include "HCSR04Gpio.hpp"
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG // Must come before esp_log.h
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

static const char *TAG = "HCSR04_GPIO";
static constexpr float SOUND_SPEED_CM_PER_US = 0.0343f;

HCSR04Gpio::HCSR04Gpio(gpio_num_t                                trig,
                       gpio_num_t                                echo,
                       const UltrasonicSensor::UltrasonicConfig &cfg)
    : HCSR04{trig, cfg}
    , echo_pin(echo)
{
}

bool HCSR04Gpio::init()
{
    ESP_LOGI(TAG, "Initializing HCSR04Gpio sensor (trig=%d, echo=%d)", trig_pin, echo_pin);

    HCSR04::init();

    gpio_reset_pin(echo_pin);

    gpio_config_t io_conf = {
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

    return true;
}

float HCSR04Gpio::readRawDistanceCm()
{
    send_ping();

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
    return pulse * SOUND_SPEED_CM_PER_US / 2.0f;
}
