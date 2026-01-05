#include "common_types.hpp"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "ota_manager.hpp"
#include "wifi_manager.hpp"
#include <stdio.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "OTA_TEST";

// Configuração do botão
#define BUTTON_GPIO GPIO_NUM_0
#define BUTTON_ACTIVE 0 // 0 = botão pressionado (BOOT button é active low)

// Variáveis globais
static bool button_pressed        = false;
static TickType_t last_press_time = 0;

auto &wifi = WiFiManager::instance();
auto &ota  = OtaManager::instance();

// Handler de eventos do OTA
static void ota_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch (id) {
    case OTA_EVT_STARTED:
        ESP_LOGI(TAG, "OTA started!");
        gpio_set_level(GPIO_NUM_2, 1); // LED ON
        break;

    case OTA_EVT_FAILED:
        ESP_LOGI(TAG, "OTA failed!");
        gpio_set_level(GPIO_NUM_2, 0); // LED OFF
        break;

    case OTA_EVT_FINISHED:
        ESP_LOGI(TAG, "OTA finished - device will restart");
        break;
    }
}

// Task para debounce do botão
static void button_task(void *arg)
{
    // Configurar GPIO do botão
    gpio_config_t io_conf = {.pin_bit_mask = (1ULL << BUTTON_GPIO),
                             .mode         = GPIO_MODE_INPUT,
                             .pull_up_en =
                                 GPIO_PULLUP_ENABLE, // Botão BOOT já tem pullup externo
                             .pull_down_en = GPIO_PULLDOWN_DISABLE,
                             .intr_type    = GPIO_INTR_DISABLE};
    gpio_config(&io_conf);

    // Configurar LED (GPIO 2 - LED onboard em muitas placas)
    gpio_reset_pin(GPIO_NUM_2);
    gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_2, 0);

    ESP_LOGI(TAG, "Button task started. Press BOOT button to start OTA.");
    ESP_LOGI(TAG, "OTA server hostname: ota-server.local:8070");

    while (1) {
        // Ler estado do botão
        int level = gpio_get_level(BUTTON_GPIO);

        // Detectar pressionamento (active low)
        if (level == BUTTON_ACTIVE && !button_pressed) {
            TickType_t now = xTaskGetTickCount();

            // Debounce: 50ms
            if ((now - last_press_time) > pdMS_TO_TICKS(150)) {
                button_pressed  = true;
                last_press_time = now;

                ESP_LOGI(TAG, "Button pressed! Starting OTA...");
                wifi.stop();
                wifi.start();
                std::string ssid, password;
                esp_err_t err = wifi.loadCredentials(ssid, password);

                if (err == ESP_OK) {
                    printf("Credenciais carregadas do NVS: SSID=%s\n", ssid.c_str());
                }
                wifi.connect(ssid, password, 10000);

                // Postar evento para iniciar OTA
                const char *hostname = "ota-server"; // Altere para seu servidor

                ota.startOtaWithMdns(hostname);
                // esp_event_post(APP_OTA_EVENT, OTA_CMD_START, (void *)hostname,
                //                strlen(hostname) + 1, 0);
            }
        }
        // Detectar soltura do botão
        else if (level != BUTTON_ACTIVE && button_pressed) {
            button_pressed = false;
        }

        vTaskDelay(pdMS_TO_TICKS(10)); // 10ms polling
    }
}

// Task para simular WiFi (para teste sem WiFi real)
// static void fake_wifi_task(void *arg)
// {
//     // Simular que WiFi está conectado após 3 segundos
//     vTaskDelay(pdMS_TO_TICKS(3000));

//     ESP_LOGI(TAG, "Simulating WiFi connected with IP...");

//     // Postar eventos como se WiFi estivesse conectado
//     esp_event_post(APP_WIFI_EVENT, WIFI_EVT_CONNECTED, nullptr, 0, 0);
//     vTaskDelay(pdMS_TO_TICKS(100));

//     // Simular que tem IP
//     char ip[] = "192.168.1.100";
//     esp_event_post(APP_WIFI_EVENT, WIFI_EVT_GOT_IP, ip, sizeof(ip), 0);

//     ESP_LOGI(TAG, "WiFi simulation complete. Ready for OTA.");

//     vTaskDelete(nullptr); // Auto-delete
// }

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Starting OTA Test Application");

    // Inicializar NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    auto &wifi = WiFiManager::instance();
    wifi.init();
    wifi.start();
    std::string config_ssid     = CONFIG_WIFI_SSID;
    std::string config_password = CONFIG_WIFI_PASSWORD;

    // Se as credenciais foram fornecidas via menuconfig, armazena-as
    if (!config_ssid.empty() && !config_password.empty()) {
        printf("Armazenando credenciais do menuconfig no NVS...\n");
        printf("SSID: %s\n", config_ssid.c_str());
        esp_err_t err = wifi.storeCredentials(config_ssid, config_password);
        if (err != ESP_OK) {
            printf("Erro ao armazenar credenciais: %s\n", esp_err_to_name(err));
        }
    }

    std::string ssid, password;
    esp_err_t err = wifi.loadCredentials(ssid, password);

    if (err == ESP_OK) {
        printf("Credenciais carregadas do NVS: SSID=%s\n", ssid.c_str());
    }
    else {
        printf("Nenhuma credencial encontrada no NVS\n");

        // Se não tem no NVS mas tem no menuconfig, usa do menuconfig
        if (!config_ssid.empty() && !config_password.empty()) {
            ssid     = config_ssid;
            password = config_password;
            printf("Usando credenciais do menuconfig\n");
        }
        else {
            printf("Erro: Nenhuma credencial disponível!\n");
            return;
        }
    }

    wifi.connect(ssid, password, 10000);

    // Inicializar OTA Manager

    ESP_ERROR_CHECK(ota.init());
    ota.setDeviceType("test_device"); // Deve corresponder ao nome no servidor

    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "OTA TEST READY");
    ESP_LOGI(TAG, "1. Server must be running on: ota-server.local:8070");
    ESP_LOGI(TAG, "2. Firmware path: /test_device/test_device.bin");
    ESP_LOGI(TAG, "3. Press BOOT button (GPIO0) to start OTA");
    ESP_LOGI(TAG, "4. LED GPIO2 will light during OTA");
    ESP_LOGI(TAG, "==========================================");

    // IMPORTANTE: Para teste REAL, comente a task fake e use WiFi real
    // Criar task fake WiFi (comente para teste com WiFi real)
    // xTaskCreate(fake_wifi_task, "fake_wifi", 4096, NULL, 1, NULL);

    // Para teste REAL com WiFi, descomente:

    // Criar task do botão
    xTaskCreate(button_task, "button_task", 4096, NULL, 2, NULL);

    // Manter task principal viva
    while (1) {
        ESP_LOGI(TAG, "Main task alive...");
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}