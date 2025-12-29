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
    // Log the configuration for debugging purposes.
    ESP_LOGI(TAG, "Init gpio=%d NO=%d active_level=%d wakeup_on=%d", cfg_.gpio,
             cfg_.normally_open, static_cast<int>(cfg_.active_level),
             static_cast<int>(cfg_.wakeup_on));

    ESP_RETURN_ON_ERROR(configureGpio(), TAG, "Failed to configure GPIO %d", cfg_.gpio);

    initialized_ = ESP_OK;

    return ESP_OK;
}

esp_err_t FloatSwitch::configureGpio()
{
    gpio_config_t cfg{};
    cfg.pin_bit_mask = (1ULL << cfg_.gpio);
    cfg.mode         = GPIO_MODE_INPUT;
    cfg.intr_type    = GPIO_INTR_DISABLE; // Interrupts are handled by the deep sleep wakeup controller.

    // The internal pull resistor is configured to oppose the active level.
    // This ensures that the pin is in a defined state when the switch contact is open.
    // - If active_level is LOW (contact pulls to GND), we need a pull-up resistor.
    // - If active_level is HIGH (contact pulls to VCC), we need a pull-down resistor.
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
    // First, determine the raw electrical state of the contact.
    bool contact_closed = isContactClosed();

    // Second, interpret the electrical state to determine the physical state (tank full/empty).
    // This logic depends on the switch type (Normally Open or Normally Closed).
    if (cfg_.normally_open) {
        // For a Normally Open (NO) switch:
        // - Contact is OPEN when the tank is full (float is up).
        // - Contact is CLOSED when the tank is empty (float is down).
        // Therefore, the tank is full if the contact is NOT closed.
        return !contact_closed;
    }
    else {
        // For a Normally Closed (NC) switch:
        // - Contact is CLOSED when the tank is full (float is up).
        // - Contact is OPEN when the tank is empty (float is down).
        // Therefore, the tank is full if the contact IS closed.
        return contact_closed;
    }
}

bool FloatSwitch::shouldEnableWakeup() const
{
    if (initialized_ != ESP_OK) {
        ESP_LOGE(TAG, "FloatSwitch not initialized, cannot determine wakeup logic");
        return false;
    }

    switch (cfg_.wakeup_on) {
    case WakeupCondition::NEVER:
        // If wake-up is never desired, simply return false.
        return false;

    case WakeupCondition::WHEN_TANK_IS_EMPTY:
        // We want to wake up when the tank becomes empty.
        // To prevent an immediate wake-up loop, we should only arm the wake-up
        // trigger if the tank is currently FULL. This prepares the system to
        // catch the transition from full to empty.
        return isTankFull();

    case WakeupCondition::WHEN_TANK_IS_FULL:
        // We want to wake up when the tank becomes full.
        // Similarly, we only arm the wake-up trigger if the tank is currently
        // EMPTY. This prepares the system to catch the transition from empty to full.
        return !isTankFull();

    default:
        // Should not be reached.
        return false;
    }
}

bool FloatSwitch::isContactClosed() const
{
    if (initialized_ != ESP_OK) {
        // This is a critical error, as the application is trying to use the
        // component without initializing it.
        ESP_LOGE(TAG, "FloatSwitch not initialized");
        abort(); // Or handle the error in a less drastic way if appropriate.
    }

    // --- Debouncing Logic ---
    // To avoid false readings from electrical noise or contact bounce,
    // we take multiple samples over a short period.
    const uint8_t samples   = 5;
    const uint32_t delay_us = 5000; // 5ms delay between samples
    uint8_t high_count      = 0;

    for (uint8_t i = 0; i < samples; i++) {
        if (gpio_get_level(cfg_.gpio)) {
            high_count++;
        }
        // A small delay is crucial for the debouncing to be effective.
        esp_rom_delay_us(delay_us);
    }

    // A stable reading is determined by a majority vote of the samples.
    // `raw_stable` will be true if the GPIO level was mostly HIGH, false if mostly LOW.
    bool raw_stable = (high_count > (samples / 2));

    // Finally, translate the stable electrical level into the contact state.
    // This depends on whether the closed contact pulls the signal LOW or HIGH.
    if (cfg_.active_level == ActiveLevel::LOW) {
        // If active level is LOW, a closed contact means the raw signal is LOW.
        return !raw_stable;
    }
    else { // ActiveLevel::HIGH
        // If active level is HIGH, a closed contact means the raw signal is HIGH.
        return raw_stable;
    }
}