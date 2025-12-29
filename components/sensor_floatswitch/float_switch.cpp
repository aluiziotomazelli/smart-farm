#include "float_switch.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "esp_check.h"
#include "esp_timer.h"

static const char *TAG = "FloatSwitch";

FloatSwitch::FloatSwitch(const Config &cfg)
    : cfg_(cfg)
{
}

esp_err_t FloatSwitch::init()
{
    ESP_LOGI(TAG, "Init gpio=%d NO=%d active_level=%d", cfg_.gpio, cfg_.normally_open,
             static_cast<int>(cfg_.active_level));

    ESP_RETURN_ON_ERROR(configureGpio(), TAG, "Failed to configure GPIO %d", cfg_.gpio);

    initialized_ = ESP_OK;

    return ESP_OK;
}

esp_err_t FloatSwitch::configureGpio()
{
    gpio_config_t cfg{};
    cfg.pin_bit_mask = (1ULL << cfg_.gpio);
    cfg.mode         = GPIO_MODE_INPUT;
    cfg.intr_type    = GPIO_INTR_DISABLE;

    // Configura pull interno de acordo com o nível ativo do contato:
    // ActiveLevel::LOW  -> contato fecha em GND  -> pull-up
    // ActiveLevel::HIGH -> contato fecha em 3V3  -> pull-down
    switch (cfg_.active_level) {
    case ActiveLevel::LOW:
        cfg.pull_up_en   = GPIO_PULLUP_ENABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        break;
    case ActiveLevel::HIGH:
        cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
        break;
    }
    esp_err_t ret = gpio_config(&cfg);
    if (ret != ESP_OK) {
        return ret;
    }

    return ESP_OK;
}

bool FloatSwitch::isTankFull()
{
    bool contact_closed = isContactClosed();

    bool tank_full;
    // Interpretação física
    if (cfg_.normally_open)
        // NO: contato fechado = Tanque vazio
        // NO: contato aberto = Tanque cheio
        tank_full = !contact_closed;

    else {
        // NC: contato fechado = Tanque cheio
        // NC: contato aberto = Tanque vazio
        tank_full = contact_closed;
    }
    return tank_full;
}

bool FloatSwitch::getWakeupInfo(WakeupInfo &info) const
{
    info.gpio_mask = 1ULL << cfg_.gpio;

    if (cfg_.wakeup_level == WakeupLevel::HIGH)
        info.mode = 1;
    else
        info.mode = 0;

    return true;
}

bool FloatSwitch::isContactClosed() const
{
    if (initialized_ != ESP_OK) {
        ESP_LOGE(TAG, "FloatSwitch not initialized");
        abort();
    }

    const uint8_t samples   = 5;
    const uint32_t delay_us = 5000;
    uint8_t high_count      = 0;

    for (uint8_t i = 0; i < samples; i++) {
        if (gpio_get_level(cfg_.gpio)) {
            high_count++;
        }
        esp_rom_delay_us(delay_us);
    }

    bool raw_stable = (high_count > (samples / 2));

    bool contact_closed;
    // If active_level == LOW, contact closed = (raw_stable == 0)
    if (cfg_.active_level == ActiveLevel::LOW) {
        contact_closed = !raw_stable;
    }
    else { // If active_level == HIGH, contact closed = (raw_stable == 1)
        contact_closed = raw_stable;
    }
    return contact_closed;
}