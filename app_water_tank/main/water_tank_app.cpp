#include "water_tank_app.hpp"
#include "float_switch.hpp"
#include "nvs_core.hpp"
#include "power_control.hpp"
#include "ultrasonic_sensor.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define POWER_GPIO GPIO_NUM_20
#define TRIGER_GPIO GPIO_NUM_21
#define ECHO_GPIO GPIO_NUM_7
#define FLOAT_SWITCH_GPIO GPIO_NUM_1
#define BATT_LEVEL_GPIO GPIO_NUM_0

static const char *TAG = "WaterTankApp";

static constexpr uint32_t ULTRASONIC_WARMUP_MS = 600;

// --- Persistence in RTC Memory ---
RTC_DATA_ATTR CoreStorage rtc_core_data;
RTC_DATA_ATTR uint16_t rtc_last_level_permille = 0;
RTC_DATA_ATTR bool rtc_has_level               = false;

WaterTankApp::WaterTankApp()
{
}

static constexpr PowerControl::Config power_cfg = {
    .gpio           = POWER_GPIO,
    .inverted_logic = true,
    .initial_on     = false,
};
static PowerControl power_to_sensor(power_cfg);

// --- Hardware Configurations ---
static constexpr UltrasonicSensor::UltrasonicConfig cfg = {
    .ping_count       = 9,
    .ping_interval_ms = 70,
    .ping_duration_us = 20,
    .timeout_us       = 25000,
    .filter           = UltrasonicSensor::Filter::DOMINANT_CLUSTER,
    .blind_ping       = false,
    .min_distance_cm  = 10.0f,
    .max_distance_cm  = 200.0f,
    .warmup_time_ms   = 600};

static UltrasonicSensor sensor(TRIGER_GPIO, ECHO_GPIO, cfg);

static FloatSwitch::Config fs_cfg = {.gpio          = FLOAT_SWITCH_GPIO,
                                     .normally_open = true,
                                     .pull          = FloatSwitch::Pull::UP,
                                     .debounce_ms   = 50,
                                     .wakeup_level  = FloatSwitch::WakeupLevel::LOW};
static FloatSwitch floatswitch(fs_cfg);

// --- Business Logic ---
FillState WaterTankApp::infer_fill_state(uint16_t current_level)
{
    if (!rtc_has_level) {
        rtc_last_level_permille = current_level;
        rtc_has_level           = true;
        ESP_LOGD(TAG, "Fill state: Unknown (First reading)");
        return FillState::UNKNOWN;
    }

    int delta               = (int)current_level - (int)rtc_last_level_permille;
    rtc_last_level_permille = current_level;

    if (delta > +LEVEL_DELTA_MIN) {
        ESP_LOGD(TAG, "Fill state: Filling, Delta: %d", delta);
        return FillState::FILLING;
    }
    if (delta < -LEVEL_DELTA_MIN) {
        ESP_LOGD(TAG, "Fill state: Draining, Delta: %d", delta);
        return FillState::DRAINING;
    }

    ESP_LOGD(TAG, "Fill state: Stable, Delta: %d", delta);
    return FillState::STABLE;
}

uint64_t WaterTankApp::decide_timer_us(FillState state)
{
    switch (state) {
    case FillState::FILLING:
        return TIMER_FILLING_US;
    case FillState::DRAINING:
        return TIMER_DRAIN_US;
    case FillState::STABLE:
        return TIMER_STABLE_US;
    case FillState::UNKNOWN:
    default:
        return TIMER_UNKNOWN_US;
    }
}

void WaterTankApp::configure_sleep_policy(bool float_switch_closed, uint64_t timer_us)
{
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

    if (timer_us > 0) {
        esp_sleep_enable_timer_wakeup(timer_us);
    }

    if (!float_switch_closed) {
        FloatSwitch::WakeupInfo wi;
        if (floatswitch.get_wakeup_info(wi)) {
            esp_deep_sleep_enable_gpio_wakeup(wi.gpio_mask,
                                              (esp_deepsleep_gpio_wake_up_mode_t)wi.mode);
        }
    }
}

static uint16_t distance_to_level_permille(float d_cm)
{
    if (d_cm >= LEVEL_MIN_CM)
        return 0;
    if (d_cm <= LEVEL_MAX_CM)
        return 1000;
    float span  = LEVEL_MIN_CM - LEVEL_MAX_CM;
    float level = (LEVEL_MIN_CM - d_cm) / span;
    return static_cast<uint16_t>(level * 1000.0f);
}

void WaterTankApp::on_espnow_receive(uint8_t node_id,
                                     const uint8_t *data,
                                     int len,
                                     int8_t rssi)
{
    ESP_LOGI(TAG, "Received %d bytes from node %u (RSSI: %d dBm)", len, node_id, rssi);
    // This device is a sensor, so it primarily sends data.
    // For now, we just log any incoming data. In the future, this could be used for
    // configuration or commands.
}

void WaterTankApp::on_espnow_send(uint8_t node_id, esp_now_send_status_t status)
{
    const char *status_str = (status == ESP_NOW_SEND_SUCCESS) ? "SUCCESS" : "FAIL";

    if (node_id == 0xFF) {
        ESP_LOGI(TAG, "Broadcast send: %s", status_str);
    }
    else {
        ESP_LOGI(TAG, "Send to node %u: %s", node_id, status_str);
    }
}

void WaterTankApp::init()
{
    ESP_LOGI(TAG, "Initializing WaterTankApp");
    storage_.init_partition();

    power_to_sensor.init();
    // ESP_RETURN_ON_ERROR(power_to_sensor.init(), TAG, "Failed to initialize sensor
    // power");
    sensor.init();
    floatswitch.init();

    if (storage_.load() != ESP_OK) {
        ESP_LOGW(TAG, "NVS load failed, performing factory reset");
        storage_.factory_reset();
    }

    auto &core = storage_.getCoreData();
    if (core.node_id == 0) {
        ESP_LOGI(TAG, "Node ID not set, generating from MAC address...");
        uint8_t mac[6];
        esp_efuse_mac_get_default(mac);
        core.node_id = mac[3] ^ mac[4] ^ mac[5];
        if (storage_.commit() == ESP_OK) {
            ESP_LOGI(TAG, "New Node ID saved to NVS.");
        }
        else {
            ESP_LOGE(TAG, "Failed to save new Node ID to NVS!");
        }
    }

    // === Inicializar comunicação ESP-NOW ===
    ESPNOWConfig config;
    config.wifi_channel       = 0; // Ou 0 para automático
    config.max_peers          = 10;
    config.ack_timeout        = 100; // ms
    config.heartbeat_interval = 0;   // Desabilitado para economia
    config.max_packet_size    = 250;

    // Adicione log para verificar a configuração
    ESP_LOGI(TAG, "ESP-NOW Config: max_packet_size=%u", config.max_packet_size);

    if (!comm_.init(config)) {
        ESP_LOGE(TAG, "Failed to initialize ESP-NOW");
    }
    else {
        ESP_LOGI(TAG, "ESP-NOW initialized. Our node ID: %u", comm_.get_id());

        // Configurar callbacks (usando lambdas ou std::bind)
        comm_.setReceiveCallback(
            [this](uint8_t node_id, const uint8_t *data, int len, int8_t rssi) {
                this->on_espnow_receive(node_id, data, len, rssi);
            });

        comm_.setSendCallback([this](uint8_t node_id, esp_now_send_status_t status) {
            this->on_espnow_send(node_id, status);
        });

        comm_.startDiscovery(10000);
    }

    esp_reset_reason_t reset_reason     = esp_reset_reason();
    esp_sleep_wakeup_cause_t wake_cause = esp_sleep_get_wakeup_cause();
    ESP_LOGI(TAG, "Reset reason: %d, Wakeup cause: %d", reset_reason, wake_cause);

    bool woke_from_sleep = (wake_cause != ESP_SLEEP_WAKEUP_UNDEFINED);

    if (!woke_from_sleep && core.boot_count > 0) {
        core.crash_count++;
    }
    core.boot_count++;

    switch (wake_cause) {
    case ESP_SLEEP_WAKEUP_EXT0:
    case ESP_SLEEP_WAKEUP_EXT1:
        core.last_wake = WakeSource::GPIO;
        ESP_LOGI(TAG, "Woke from GPIO");
        break;
    case ESP_SLEEP_WAKEUP_TIMER:
        core.last_wake = WakeSource::TIMER;
        ESP_LOGI(TAG, "Woke from TIMER");
        break;
    case ESP_SLEEP_WAKEUP_UNDEFINED:
    default:
        core.last_wake = WakeSource::POWER_ON;
        ESP_LOGI(TAG, "Power-on reset or undefined wake");
        break;
    }

    rtc_core_data = core;
    ESP_LOGI(TAG, "Boot #%lu (crashes: %lu)", rtc_core_data.boot_count,
             rtc_core_data.crash_count);

    vTaskDelay(pdMS_TO_TICKS(100));
}

void WaterTankApp::run()
{
    ESP_LOGI(TAG, "Starting Application Loop");

    while (true) {
        auto &core = storage_.getCoreData();
        auto &app  = storage_.stats;

        float distance    = 0.0f;
        UsQuality quality = UsQuality::INVALID;
        UsFailure failure = UsFailure::NONE;
        uint16_t level    = 0;

        // ESP_RETURN_ON_ERROR(power_to_sensor.on(), TAG, "Failed to power on sensor");
        power_to_sensor.on();
        // vTaskDelay(pdMS_TO_TICKS(ULTRASONIC_WARMUP_MS));
        bool ok = sensor.readDistance_cm(distance, quality, failure);
        // ESP_RETURN_ON_ERROR(power_to_sensor.off(), TAG, "Failed to power off sensor");
        power_to_sensor.off();

        if (ok) {
            level = distance_to_level_permille(distance);
            if (quality == UsQuality::WEAK) {
                ESP_LOGW(TAG, "Ultrasonic WEAK reading: %.2f cm (level: %u‰)", distance,
                         level);
            }
            else {
                ESP_LOGI(TAG, "Distance: %.2f cm, Level: %u‰", distance, level);
            }
        }
        else {
            ESP_LOGW(TAG, "Ultrasonic INVALID reading: %s",
                     us_failure_to_string(failure));
        }

        storage_.updateStatus(level, distance, quality, failure);
        app.fill_state = infer_fill_state(level);

        uint64_t timer_us = decide_timer_us(app.fill_state);
        if (quality == UsQuality::WEAK)
            timer_us = static_cast<uint64_t>(timer_us * WEAK_SLEEP_FACTOR);
        else if (quality == UsQuality::INVALID)
            timer_us = static_cast<uint64_t>(timer_us * INVALID_SLEEP_FACTOR);
        core.sleep_interval_s          = (uint32_t)(timer_us / 1000000ULL);
        rtc_core_data.sleep_interval_s = core.sleep_interval_s;

        bool float_switch_closed = floatswitch.read();
        app.gpio_wakeup_enabled  = !float_switch_closed;
        configure_sleep_policy(float_switch_closed, timer_us);

        app.sample_uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);

        core = rtc_core_data;
        // _storage.commit();

        // === Enviar dados via ESP-NOW ===
        uint16_t payload = level;
        auto peers       = comm_.getPeers();

        if (peers.empty()) {
            if (!comm_.broadcast(reinterpret_cast<const uint8_t *>(&payload),
                                 sizeof(payload))) {
                ESP_LOGE(TAG, "Failed to send broadcast");
            }
        }
        else {
            for (const auto &peer : peers) {
                if (!comm_.send(peer.node_id, reinterpret_cast<const uint8_t *>(&payload),
                                sizeof(payload))) {
                    ESP_LOGE(TAG, "Failed to send to peer %u", peer.node_id);
                }
            }
        }

        // Processar tarefas internas (timeouts, etc)
        comm_.process();

        ESP_LOGI(TAG,
                 "Summary - Level: %u‰, Measures: %lu (OK:%lu WEAK:%lu INV:%lu), Boot: "
                 "%lu, Crash: %lu",
                 app.level_permille, app.measure_count, app.ok_count, app.weak_count,
                 app.invalid_count, core.boot_count, core.crash_count);

        vTaskDelay(pdMS_TO_TICKS(2000));
        // esp_deep_sleep_start();
    }
}
