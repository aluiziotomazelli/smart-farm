#include "float_switch.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "FloatSwitch";

static uint64_t now_ms()
{
    return esp_timer_get_time() / 1000;
}

FloatSwitch::FloatSwitch(const Config &cfg)
    : config(cfg)
{
}

bool FloatSwitch::init()
{
    ESP_LOGI(TAG, "Init gpio=%d NO=%d pull=%d", config.gpio, config.normally_open,
             static_cast<int>(config.pull));

    if (!configure_gpio()) {
        return false;
    }

    bool raw           = read_raw();
    stable_state       = raw;
    pending_state      = raw;
    last_transition_ms = now_ms();

    initialized = true;
    return true;
}

bool FloatSwitch::configure_gpio()
{
    gpio_config_t cfg{};
    cfg.pin_bit_mask = (1ULL << config.gpio);
    cfg.mode         = GPIO_MODE_INPUT;
    cfg.intr_type    = GPIO_INTR_DISABLE;

    // Configura pull-up/pull-down conforme especificado
    // Estas funções funcionam tanto para GPIOs normais quanto RTC GPIOs
    switch (config.pull) {
    case Pull::UP:
        cfg.pull_up_en   = GPIO_PULLUP_ENABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        break;
    case Pull::DOWN:
        cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
        break;
    case Pull::NONE:
        cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        break;
    }

    esp_err_t ret = gpio_config(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO %d: %d", config.gpio, ret);
        return false;
    }

    return true;
}

bool FloatSwitch::read_raw() const
{
    return gpio_get_level(config.gpio);
}

bool FloatSwitch::debounce_update(bool raw)
{
    uint64_t t = now_ms();

    if (raw != pending_state) {
        pending_state      = raw;
        last_transition_ms = t;
        return false;
    }

    if ((t - last_transition_ms) >= config.debounce_ms && stable_state != pending_state) {
        stable_state = pending_state;
        return true;
    }

    return false;
}

bool FloatSwitch::read()
{
    if (!initialized) {
        return false;
    }

    bool raw = read_raw();
    debounce_update(raw);

    // Interpretação física
    if (config.normally_open) {
        // NO: LOW = contato fechado = água
        return !stable_state;
    }
    else {
        // NC: HIGH = contato aberto = água
        return stable_state;
    }
}

bool FloatSwitch::get_wakeup_info(WakeupInfo &info) const
{
    info.gpio_mask = 1ULL << config.gpio;

    if (config.wakeup_level == WakeupLevel::HIGH)
        info.mode = 1;
    else
        info.mode = 0;

    return true;
}