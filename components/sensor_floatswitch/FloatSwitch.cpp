#include "FloatSwitch.hpp"
#include "driver/rtc_io.h"
#define LOG_LOCAL_LEVEL ESP_LOG_INFO // Must come before esp_log.h
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/rtc.h"

static const char *TAG = "FloatSwitch";

FloatSwitch::FloatSwitch(const Config &config)
    : config(config)
{
    // Check if pin is RTC capable
    rtc_capable = rtc_gpio_is_valid_gpio(config.pin);

    if (!rtc_capable)
    {
        ESP_LOGW(TAG, "GPIO %d is not RTC capable - wakeup from deep sleep won't work", config.pin);
    }
}

FloatSwitch::~FloatSwitch()
{
    if (initialized)
    {
        disable_wakeup();
    }
}

bool FloatSwitch::init()
{
    ESP_LOGI(TAG, "Initializing float switch on pin %d (NO: %s, RTC: %s)", config.pin,
             config.normally_open ? "true" : "false", rtc_capable ? "yes" : "no");

    // For deep sleep wakeup, we MUST use RTC_GPIO functions
    if (rtc_capable)
    {
        return configure_rtc_gpio();
    }
    else
    {
        // Fallback to normal GPIO (for systems not using deep sleep)
        gpio_config_t io_conf = {};
        io_conf.pin_bit_mask  = (1ULL << config.pin);
        io_conf.mode          = GPIO_MODE_INPUT;

        // Configure pull based on normally_open setting
        if (config.normally_open)
        {
            // NA switch: use internal pull-up, switch closes to GND when water is high
            io_conf.pull_up_en   = GPIO_PULLUP_ENABLE;
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        }
        else
        {
            // NC switch: use internal pull-down, switch opens to VCC when water is high
            io_conf.pull_up_en   = GPIO_PULLUP_DISABLE;
            io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
        }

        io_conf.intr_type = GPIO_INTR_DISABLE;

        esp_err_t err = gpio_config(&io_conf);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to configure GPIO %d: %s", config.pin, esp_err_to_name(err));
            return false;
        }
    }

    // Read initial state
    last_state  = read_raw();
    initialized = true;

    ESP_LOGI(TAG, "Initial state: %s", last_state ? "HIGH" : "LOW");
    return true;
}

bool FloatSwitch::configure_rtc_gpio()
{
    ESP_LOGD(TAG, "Configuring RTC_GPIO %d", config.pin);

    // Initialize RTC_GPIO
    esp_err_t err = rtc_gpio_init(config.pin);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize RTC_GPIO %d: %s", config.pin, esp_err_to_name(err));
        return false;
    }

    // Set as input
    err = rtc_gpio_set_direction(config.pin, RTC_GPIO_MODE_INPUT_ONLY);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set RTC_GPIO direction: %s", esp_err_to_name(err));
        return false;
    }

    // Disable output
    err = rtc_gpio_set_direction_in_sleep(config.pin, RTC_GPIO_MODE_INPUT_ONLY);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set RTC_GPIO sleep direction: %s", esp_err_to_name(err));
        return false;
    }

    // Configure pull based on normally_open setting
    if (config.normally_open)
    {
        // NA switch: pull-up, closes to GND when water is high
        rtc_gpio_pullup_en(config.pin);
        rtc_gpio_pulldown_dis(config.pin);
    }
    else
    {
        // NC switch: pull-down, opens to VCC when water is high
        rtc_gpio_pullup_dis(config.pin);
        rtc_gpio_pulldown_en(config.pin);
    }

    // Disable hold (important for wakeup)
    rtc_gpio_hold_dis(config.pin);

    initialized = true;
    return true;
}

void FloatSwitch::release_rtc_gpio()
{
    if (rtc_capable && initialized)
    {
        ESP_LOGD(TAG, "Releasing RTC_GPIO %d", config.pin);
        rtc_gpio_hold_dis(config.pin);
        rtc_gpio_deinit(config.pin);
    }
}

bool FloatSwitch::read_raw()
{
    if (rtc_capable)
    {
        return rtc_gpio_get_level(config.pin);
    }
    else
    {
        return gpio_get_level(config.pin);
    }
}

bool FloatSwitch::debounce_check(bool current_state)
{
    uint64_t now = esp_timer_get_time() / 1000; // Convert to milliseconds

    if (current_state != last_state)
    {
        if (now - last_change_time > config.debounce_ms)
        {
            // State change is stable
            last_state       = current_state;
            last_change_time = now;
            ESP_LOGD(TAG, "State changed to: %s", current_state ? "HIGH" : "LOW");
            return true;
        }
    }
    else
    {
        // Reset debounce timer if state is stable
        last_change_time = now;
    }

    return false;
}

bool FloatSwitch::read()
{
    if (!initialized)
    {
        ESP_LOGW(TAG, "Not initialized, call init() first");
        return false;
    }

    bool current_state = read_raw();
    debounce_check(current_state);
    return last_state;
}

bool FloatSwitch::is_level_high()
{
    if (!initialized)
    {
        return false;
    }

    bool raw_state = read_raw();

    // Logic based on normally_open configuration:
    if (config.normally_open)
    {
        // NO: LOW = switch closed = water present
        return !raw_state;
    }
    else
    {
        // NC: HIGH = switch open = water present
        return raw_state;
    }
}

bool FloatSwitch::enable_wakeup()
{
    if (!rtc_capable)
    {
        ESP_LOGE(TAG, "Pin %d is not RTC capable, cannot enable wakeup", config.pin);
        return false;
    }

    if (!initialized)
    {
        ESP_LOGE(TAG, "Not initialized, call init() first");
        return false;
    }

    ESP_LOGI(TAG, "Configuring wakeup on pin %d (edge: %s)", config.pin,
             config.wakeup_edge == WakeupEdge::FALLING  ? "FALLING"
             : config.wakeup_edge == WakeupEdge::RISING ? "RISING"
                                                        : "ANY");

    // Determine wakeup level based on configuration and normally_open
    int wakeup_level = 0;

    if (config.wakeup_edge == WakeupEdge::RISING)
    {
        wakeup_level = 1; // Wake on HIGH level
    }
    else if (config.wakeup_edge == WakeupEdge::FALLING)
    {
        wakeup_level = 0; // Wake on LOW level
    }
    else
    {
        // For ANY edge, we need to use EXT1 wakeup with multiple pins
        // Since we only have one pin, we'll default to wake on both?
        // ESP32 doesn't support both edges in ext0, so we'll use LOW
        ESP_LOGW(TAG, "ANY edge not supported for single pin, using LOW");
        wakeup_level = 0;
    }

    // Configure wakeup from deep sleep
    esp_err_t err = esp_sleep_enable_ext0_wakeup(config.pin, wakeup_level);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to enable ext0 wakeup: %s", esp_err_to_name(err));
        return false;
    }

    // Enable hold on RTC_GPIO to maintain configuration during sleep
    rtc_gpio_hold_en(config.pin);

    ESP_LOGI(TAG, "Wakeup configured on pin %d (level: %d)", config.pin, wakeup_level);
    return true;
}

bool FloatSwitch::disable_wakeup()
{
    if (!rtc_capable || !initialized)
    {
        return false;
    }

    ESP_LOGD(TAG, "Disabling wakeup on pin %d", config.pin);

    // Disable hold
    rtc_gpio_hold_dis(config.pin);

    return true;
}