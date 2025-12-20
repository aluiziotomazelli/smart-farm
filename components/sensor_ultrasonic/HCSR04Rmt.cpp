#include "HCSR04Rmt.hpp"
#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "HCSR04_RMT";

HCSR04Rmt::HCSR04Rmt(gpio_num_t                                trig,
                     gpio_num_t                                echo,
                     const UltrasonicSensor::UltrasonicConfig &cfg)
    : HCSR04{trig, cfg}
    , echo_pin(echo)
    , rx_channel(nullptr)
{
}

bool HCSR04Rmt::init()
{
    ESP_LOGI(TAG, "Initializing HCSR04Rmt (trig=%d, echo=%d)", trig_pin, echo_pin);

    if (!HCSR04::init())
    {
        ESP_LOGE(TAG, "Failed to initialize base HCSR04");
        return false;
    }

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

bool HCSR04Rmt::readRawDistanceCm(float &out_cm, UsFailure &out_failure)
{
    rmt_receive_config_t rx_cfg = {};
    rx_cfg.signal_range_min_ns  = 1000;
    rx_cfg.signal_range_max_ns  = cfg.timeout_us * 1000u;
    rx_cfg.flags.en_partial_rx  = false;

    send_ping();

    if (rmt_receive(rx_channel, rx_symbols, sizeof(rx_symbols), &rx_cfg) != ESP_OK)
    {
        out_failure = UsFailure::HW_ERROR;
        return false;
    }

    for (size_t i = 0; i < 64; i++)
    {
        const rmt_symbol_word_t &s = rx_symbols[i];

        if (s.duration0 == 0 && s.duration1 == 0)
            break;

        if (s.level0 == 1 && s.duration0 > 0)
        {
            out_cm      = s.duration0 * SOUND_SPEED_CM_PER_US / 2.0f;
            out_failure = UsFailure::NONE;
            return true;
        }

        if (s.level1 == 1 && s.duration1 > 0)
        {
            out_cm      = s.duration1 * SOUND_SPEED_CM_PER_US / 2.0f;
            out_failure = UsFailure::NONE;
            return true;
        }
    }

    out_failure = UsFailure::TIMEOUT;
    return false;
}
