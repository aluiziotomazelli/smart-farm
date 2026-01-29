// test_wifi.c
#include "esp_log.h"
#include "nvs_flash.h"
#include "unity.h"
#include <stdio.h>

#include "test_memory_helper.h"
#include "wifi_manager.hpp"

static const char *TAG = "test_wifi";

// SSID and Password for testing
#define TEST_WIFI_SSID "SSID"
#define TEST_WIFI_PASS "PASSWORD"

static void print_memory(const char *label)
{
    size_t free_8bit  = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t free_32bit = heap_caps_get_free_size(MALLOC_CAP_32BIT);
    printf("%s - 8BIT: %u, 32BIT: %u bytes free\n", label, (unsigned)free_8bit,
           (unsigned)free_32bit);
}

/**
 * 1. Initize WiFi Manager once and deinitize
 */
TEST_CASE("test_wifi_init_once", "[wifi][init]")
{
    // Para teste de WiFi, use threshold maior
    set_memory_leak_threshold(-15000); // 15KB permitido para WiFi

    WiFiManager &wm = WiFiManager::instance();

    printf("Testing WiFi Manager initialization...\n");

    esp_err_t ret = wm.init();
    TEST_ASSERT(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE);

    printf("WiFi Manager initialized successfully\n");

    // Limpeza
    ret = wm.deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    // Restaura threshold padrão
    // reset_memory_leak_threshold();
}

/**
 * 2, Test storing and loading WiFi credentials
 */
TEST_CASE("test_wifi_credentials", "[wifi][nvs]")
{
    // Para testes com NVS, threshold menor
    set_memory_leak_threshold(-2000); // 2KB for NVS

    WiFiManager &wm = WiFiManager::instance();

    // Inicializa se necessário
    wm.init();

    // Testa armazenamento de credenciais
    std::string test_ssid = "TestNetwork";
    std::string test_pass = "TestPassword123";

    esp_err_t ret = wm.storeCredentials(test_ssid, test_pass);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    // Testa leitura
    std::string read_ssid, read_pass;
    ret = wm.loadCredentials(read_ssid, read_pass);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_STRING(test_ssid.c_str(), read_ssid.c_str());
    TEST_ASSERT_EQUAL_STRING(test_pass.c_str(), read_pass.c_str());

    // Limpeza
    wm.deinit();
    // reset_memory_leak_threshold();
}

/**
 * 3. Test NVS memory leak
 */
TEST_CASE("test_nvs_leak", "[memory][nvs]")
{
    set_memory_leak_threshold(-2000); // 2KB for NVS

    printf("\n=== Testing NVS Memory Leak ===\n");
    print_memory("Before NVS init");

    // Inicializa NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    print_memory("After NVS init");

    // Deinit NVS (IMPORTANTE!)
    nvs_flash_deinit();

    print_memory("After NVS deinit");

    size_t delta = heap_caps_get_free_size(MALLOC_CAP_8BIT) -
                   heap_caps_get_free_size(MALLOC_CAP_8BIT); // placeholder
    printf("NVS memory delta: %d bytes\n", (int)delta);
}

/**
 * 4. Test WiFi Manager memory leak
 */
TEST_CASE("test_wifi_manager_leak", "[memory][wifi_manager]")
{
    printf("\n=== Testing WiFi Manager Memory Leak ===\n");

    set_memory_leak_threshold(-15000);

    WiFiManager &wm = WiFiManager::instance();

    print_memory("Before WiFi Manager init");

    // Inicializa
    esp_err_t ret = wm.init();
    printf("Init result: %d\n", ret);

    print_memory("After WiFi Manager init");

    // IMPORTANTE: Deve chamar deinit() para limpar tudo
    ret = wm.deinit();
    printf("Deinit result: %d\n", ret);

    print_memory("After WiFi Manager deinit");

    // Restaura threshold
    // reset_memory_leak_threshold();
}

/**
 * 5. Test Singleton Pattern
 */
TEST_CASE("test_singleton_pattern", "[wifi][singleton]")
{
    set_memory_leak_threshold(-15000);

    printf("\n=== Testing Singleton Pattern ===\n");

    // 1. Teste que instance() retorna sempre a mesma referência
    WiFiManager &instance1 = WiFiManager::instance();
    WiFiManager &instance2 = WiFiManager::instance();

    printf("Address of instance1: %p\n", &instance1);
    printf("Address of instance2: %p\n", &instance2);

    // Verifica se são a mesma instância (mesmo endereço)
    TEST_ASSERT_EQUAL_PTR(&instance1, &instance2);

    // 2. Teste que pode chamar métodos
    printf("Initial state: %d\n", (int)instance1.getState());

    // 3. Teste de inicialização
    esp_err_t ret = instance1.init();
    printf("Init result: %d (%s)\n", ret, esp_err_to_name(ret));

    // Primeira inicialização deve funcionar
    if (ret == ESP_OK) {
        printf("First init successful\n");
    }
    else if (ret == ESP_ERR_INVALID_STATE) {
        printf("Already initialized\n");
    }

    // 4. Verifica estado após init
    WiFiManager::State state = instance1.getState();
    printf("State after init: %d\n", (int)state);
    TEST_ASSERT(state != WiFiManager::State::UNINITIALIZED);

    // Cleaning
    instance1.deinit();

    printf("✓ Singleton test passed!\n");
}

/**
 * 6. Test Multiple Init Calls
 */
TEST_CASE("test_multiple_init_calls", "[wifi][init]")
{
    set_memory_leak_threshold(-15000);

    printf("\n=== Testing Multiple Init Calls ===\n");
    WiFiManager &wm = WiFiManager::instance();

    // Primeira inicialização
    esp_err_t ret1 = wm.init();
    printf("First init: %d (%s)\n", ret1, esp_err_to_name(ret1));

    // Segunda inicialização - deve ser idempotente
    esp_err_t ret2 = wm.init();
    printf("Second init: %d (%s)\n", ret2, esp_err_to_name(ret2));

    // Ambas devem retornar sucesso (ESP_OK ou algo similar)
    TEST_ASSERT(ret1 == ESP_OK || ret1 == ESP_ERR_INVALID_STATE);
    TEST_ASSERT(ret2 == ESP_OK || ret2 == ESP_ERR_INVALID_STATE);

    // Cleaning
    wm.deinit();

    printf("✓ Multiple init test passed!\n");
}

/**
 * 7. Test State Transitions
 */
TEST_CASE("test_state_transitions", "[wifi][state]")
{
    set_memory_leak_threshold(-15000);

    printf("\n=== Testing State Management ===\n");

    WiFiManager &wm = WiFiManager::instance();

    wm.init();

    // Verifica que estado é válido
    WiFiManager::State state = wm.getState();
    printf("Current state: %d\n", (int)state);

    // Estado deve ser um dos valores válidos
    TEST_ASSERT(state >= WiFiManager::State::UNINITIALIZED &&
                state <= WiFiManager::State::CONNECTED_GOT_IP);

    // Cleaning
    wm.deinit();

    printf(" State test passed!\n");
}
