#include "float_switch.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/rtc_io.h"

static const char *TAG = "FloatSwitch";

static uint64_t now_ms()
{
    return esp_timer_get_time() / 1000;
}

FloatSwitch::FloatSwitch(const Config &cfg)
    : config(cfg)
{
    rtc_capable = rtc_gpio_is_valid_gpio(config.pin);
}

FloatSwitch::~FloatSwitch()
{
    release_rtc_gpio();
}

bool FloatSwitch::init()
{
    ESP_LOGI(TAG, "Init pin=%d NO=%d RTC=%d", config.pin, config.normally_open,
             rtc_capable);

    bool ok = rtc_capable ? configure_rtc_gpio() : configure_gpio();

    if (!ok) {
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
    cfg.pin_bit_mask = (1ULL << config.pin);
    cfg.mode         = GPIO_MODE_INPUT;

    if (config.normally_open) {
        cfg.pull_up_en   = GPIO_PULLUP_ENABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    }
    else {
        cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
    }

    return gpio_config(&cfg) == ESP_OK;
}

bool FloatSwitch::configure_rtc_gpio()
{
    if (config.pull == Pull::UP) {
        rtc_gpio_pullup_en(config.pin);
        rtc_gpio_pulldown_dis(config.pin);
    }
    else if (config.pull == Pull::DOWN) {
        rtc_gpio_pullup_dis(config.pin);
        rtc_gpio_pulldown_en(config.pin);
    }
    else {
        rtc_gpio_pullup_dis(config.pin);
        rtc_gpio_pulldown_dis(config.pin);
    }

    rtc_gpio_hold_dis(config.pin);
    return true;
}

void FloatSwitch::release_rtc_gpio()
{
    if (rtc_capable && initialized) {
        rtc_gpio_hold_dis(config.pin);
        rtc_gpio_deinit(config.pin);
    }
}

bool FloatSwitch::read_raw() const
{
    return rtc_capable ? rtc_gpio_get_level(config.pin) : gpio_get_level(config.pin);
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

    // interpretação física
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
    if (!rtc_capable) {
        return false;
    }

    info.pin = config.pin;

    if (config.wakeup_edge == WakeupLevel::HIGH)
        info.level = 1;
    else
        info.level = 0;

    return true;
}
