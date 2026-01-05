#include "esp_log.h"
#include "nvs_flash.h"
#include "ota_manager.hpp"
#include "wifi_manager.hpp"
#include <stdio.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// This event base is not used in this example, but defined in common_types for other apps
#include "common_types.hpp"

static const char *TAG = "OTA_TEST";

// Button configuration
#define BUTTON_GPIO GPIO_NUM_0
#define BUTTON_ACTIVE 0 // 0 = pressed (BOOT button is active low)
#define LED_GPIO GPIO_NUM_2

// Globals
static bool button_pressed        = false;
static TickType_t last_press_time = 0;

auto &wifi = WiFiManager::instance();
auto &ota  = OtaManager::instance();

// OTA Event handler to provide user feedback (e.g., turn on LED)
static void ota_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch (id) {
    case OTA_EVT_STARTED:
        ESP_LOGI(TAG, "OTA event: Download started!");
        gpio_set_level(LED_GPIO, 1); // LED ON
        break;

    case OTA_EVT_FAILED:
        ESP_LOGE(TAG, "OTA event: Download failed!");
        gpio_set_level(LED_GPIO, 0); // LED OFF
        // After failure, WiFi should be reset for ESP-NOW
        ESP_LOGI(TAG, "Resetting WiFi for ESP-NOW operation after OTA failure.");
        wifi.stop();
        wifi.start();
        break;

    case OTA_EVT_FINISHED:
        ESP_LOGI(TAG, "OTA event: Finished! Device will restart.");
        // LED will stay on until reboot
        break;
    }
}

// Task to handle button press for starting OTA
static void button_task(void *arg)
{
    // Configure button GPIO
    gpio_config_t io_conf = {.pin_bit_mask = (1ULL << BUTTON_GPIO),
                             .mode         = GPIO_MODE_INPUT,
                             .pull_up_en   = GPIO_PULLUP_ENABLE,
                             .pull_down_en = GPIO_PULLDOWN_DISABLE,
                             .intr_type    = GPIO_INTR_DISABLE};
    gpio_config(&io_conf);

    // Configure LED (GPIO 2 - onboard LED on many boards)
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 0);

    ESP_LOGI(TAG, "Button task started. Press BOOT button to start OTA.");
    ESP_LOGI(TAG, "OTA server hostname: ota-server.local:8070");

    while (1) {
        int level = gpio_get_level(BUTTON_GPIO);

        if (level == BUTTON_ACTIVE && !button_pressed) {
            TickType_t now = xTaskGetTickCount();

            // Debounce: 150ms
            if ((now - last_press_time) > pdMS_TO_TICKS(150)) {
                button_pressed  = true;
                last_press_time = now;

                ESP_LOGI(TAG, "Button pressed! Starting OTA sequence...");

                // The flow for OTA is: stop wifi (to clear esp-now channel), start, connect.
                ESP_LOGI(TAG, "Stopping WiFi...");
                if (wifi.stop(5000) != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to stop WiFi. Aborting OTA.");
                    continue;
                }

                ESP_LOGI(TAG, "Starting WiFi...");
                if (wifi.start(5000) != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to start WiFi. Aborting OTA.");
                    continue;
                }

                std::string ssid, password;
                esp_err_t err = wifi.loadCredentials(ssid, password);

                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "No credentials found. Aborting OTA.");
                    continue;
                }

                ESP_LOGI(TAG, "Credentials loaded. Connecting to '%s'...", ssid.c_str());
                err = wifi.connect(ssid, password, 15000); // 15 sec timeout

                if (err == ESP_OK) {
                    ESP_LOGI(TAG, "WiFi connected successfully. Starting OTA download.");
                    const char *hostname = "ota-server";
                    ota.startOtaWithMdns(hostname);
                }
                else {
                    ESP_LOGE(TAG, "Failed to connect to WiFi: %s. Aborting OTA.",
                             esp_err_to_name(err));
                    // The ota_event_handler will handle the failure and restart wifi
                }
            }
        }
        else if (level != BUTTON_ACTIVE && button_pressed) {
            button_pressed = false;
        }

        vTaskDelay(pdMS_TO_TICKS(10)); // 10ms polling
    }
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Starting OTA Test Application");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize WiFi and OTA Managers
    wifi.init();
    ota.init();
    ota.setDeviceType("test_device"); // Must match the name on the server

    // Register our custom handler to give feedback on OTA events
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        APP_OTA_EVENT, ESP_EVENT_ANY_ID, &ota_event_handler, nullptr, nullptr));

    // Start WiFi in STA mode for ESP-NOW. This test app will also connect.
    ESP_LOGI(TAG, "Starting WiFi in STA mode...");
    if (wifi.start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi. Halting.");
        return;
    }

    // Load credentials from NVS or menuconfig
    std::string config_ssid     = CONFIG_WIFI_SSID;
    std::string config_password = CONFIG_WIFI_PASSWORD;

    if (!config_ssid.empty()) {
        printf("Storing credentials from menuconfig into NVS...\n");
        esp_err_t err = wifi.storeCredentials(config_ssid, config_password);
        if (err != ESP_OK) {
            printf("Error storing credentials: %s\n", esp_err_to_name(err));
        }
    }

    std::string ssid, password;
    if (wifi.loadCredentials(ssid, password) == ESP_OK) {
        printf("Credentials loaded from NVS: SSID=%s\n", ssid.c_str());
        // Attempt to connect
        if (wifi.connect(ssid, password, 10000) == ESP_OK) {
            ESP_LOGI(TAG, "Initial WiFi connection successful.");
        }
        else {
            ESP_LOGW(TAG, "Initial WiFi connection failed. WiFi remains started for ESP-NOW.");
        }
    }
    else {
        ESP_LOGW(TAG,
                 "No WiFi credentials found in NVS. WiFi remains started for ESP-NOW.");
    }

    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "OTA TEST READY");
    ESP_LOGI(TAG, "1. Server must be running on: ota-server.local:8070");
    ESP_LOGI(TAG, "2. Firmware path: /test_device/test_device.bin");
    ESP_LOGI(TAG, "3. Press BOOT button (GPIO0) to start OTA");
    ESP_LOGI(TAG, "4. LED (GPIO%d) will light up during OTA", LED_GPIO);
    ESP_LOGI(TAG, "==========================================");

    xTaskCreate(button_task, "button_task", 4096, NULL, 5, NULL);

    while (1) {
        ESP_LOGI(TAG, "Main task alive... Current WiFi State: %d", (int)wifi.getState());
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}