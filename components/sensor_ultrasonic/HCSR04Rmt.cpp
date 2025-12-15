#include "HCSR04Rmt.hpp"
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "HCSR04_RMT";

HCSR04Rmt::HCSR04Rmt(gpio_num_t                                trig,
                     gpio_num_t                                echo,
                     const UltrasonicSensor::UltrasonicConfig &cfg)
    : UltrasonicSensor{cfg}
    , trig_pin(trig)
    , echo_pin(echo)
    , rx_channel(nullptr)
{
}

bool HCSR04Rmt::init()
{
    ESP_LOGI(TAG, "Initializing HCSR04Rmt (trig=%d, echo=%d)", trig_pin, echo_pin);

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

    rmt_rx_channel_config_t rx_cfg;
    memset(&rx_cfg, 0, sizeof(rx_cfg));

    rx_cfg.gpio_num           = echo_pin;
    rx_cfg.clk_src            = RMT_CLK_SRC_DEFAULT;
    rx_cfg.resolution_hz      = 1 * 1000 * 1000; // 1 µs
    rx_cfg.mem_block_symbols  = 64;
    rx_cfg.intr_priority      = 0;
    rx_cfg.flags.invert_in    = false;
    rx_cfg.flags.with_dma     = false;
    rx_cfg.flags.io_loop_back = false;
    rx_cfg.flags.allow_pd     = false;

    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_cfg, &rx_channel));
    ESP_ERROR_CHECK(rmt_enable(rx_channel));

    return true;
}

float HCSR04Rmt::readRawDistanceCm()
{
    rmt_receive_config_t rx_cfg;
    memset(&rx_cfg, 0, sizeof(rx_cfg));

    rx_cfg.signal_range_min_ns = 1000;
    rx_cfg.signal_range_max_ns = cfg.timeout_us * 1000u;
    rx_cfg.flags.en_partial_rx = false;

    gpio_set_level(trig_pin, 1);
    esp_rom_delay_us(cfg.ping_duration_us);
    gpio_set_level(trig_pin, 0);

    if (rmt_receive(rx_channel, rx_symbols, sizeof(rx_symbols), &rx_cfg) != ESP_OK)
        return -1;

    for (size_t i = 0; i < 64; i++)
    {
        const rmt_symbol_word_t &s = rx_symbols[i];

        if (s.duration0 == 0 && s.duration1 == 0)
            break;

        if (s.level0 == 1 && s.duration0 > 0)
            return s.duration0 * 0.0343f / 2.0f;

        if (s.level1 == 1 && s.duration1 > 0)
            return s.duration1 * 0.0343f / 2.0f;
    }

    return -1;
}
