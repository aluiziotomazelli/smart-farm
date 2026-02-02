// test_wifi_manager.cpp
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "unity.h"

#include "secrets.h"
#include "test_memory_helper.h"
#include <cstring>

#include "wifi_manager.hpp"

extern "C" void test_warmup(void)
{
    printf("\n=== WiFiManager Warmup ===\n");
    printf("Pre-allocating WiFi, NVS and Netif internal buffers...\n");
    WiFiManager &wm = WiFiManager::instance();
    wm.init();
    wm.start(5000);
    wm.stop(5000);
    wm.deinit();
    printf("Warmup complete. Memory state stabilized.\n");

    // Default to ERROR for tests, can be changed via specific test cases
    esp_log_level_set("*", ESP_LOG_ERROR);
    printf("Log level set to ERROR for all components.\n");
    printf("==========================\n\n");
}

static void print_memory(const char *label)
{
    size_t free_8bit  = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t free_32bit = heap_caps_get_free_size(MALLOC_CAP_32BIT);
    printf("%s - 8BIT: %u, 32BIT: %u bytes free\n", label, (unsigned)free_8bit,
           (unsigned)free_32bit);
}

// ========================================================================
// 0. LOG CONTROLS
// ========================================================================
/**
 * 0.2 Turn logs on
 */
TEST_CASE("LOG on", "[wifi][internal][log]")
{
    esp_log_level_set("*", ESP_LOG_DEBUG);
}

/**
 * 0.1 Turn logs off
 */
TEST_CASE("LOG off", "[wifi][internal][log]")
{
    esp_log_level_set("*", ESP_LOG_ERROR);
}

// ========================================================================
// GROUP 1: NVS AND CREDENTIALS
// ========================================================================

/**
 * 1. Initialize WiFi Manager once and deinitialize
 */
TEST_CASE("test_wifi_init_once", "[wifi][init]")
{
    set_memory_leak_threshold(-2000);
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();

    printf("Testing WiFi Manager initialization...\n");
    esp_err_t ret = wm.init();
    TEST_ASSERT(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE);

    ret = wm.deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

/**
 * 2. Test setting and getting WiFi credentials
 */
TEST_CASE("test_wifi_credentials", "[wifi][nvs]")
{
    set_memory_leak_threshold(-2000);
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();

    std::string test_ssid = "TestNetwork";
    std::string test_pass = "TestPassword123";

    printf("Setting credentials: SSID=%s\n", test_ssid.c_str());
    esp_err_t ret = wm.setCredentials(test_ssid, test_pass);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    printf("Getting credentials from Driver...\n");
    std::string read_ssid, read_pass;
    ret = wm.getCredentials(read_ssid, read_pass);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_STRING(test_ssid.c_str(), read_ssid.c_str());
    TEST_ASSERT_EQUAL_STRING(test_pass.c_str(), read_pass.c_str());

    wm.deinit();
}

/**
 * 3. Test Credentials Deep (Max Lengths)
 */
TEST_CASE("test_credentials_deep", "[wifi][nvs]")
{
    set_memory_leak_threshold(-2000);
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();

    // SSID: 32 chars, Password: 64 chars
    std::string max_ssid(32, 'S');
    std::string max_pass(64, 'P');

    printf("Testing 32-char SSID and 64-char Password...\n");
    esp_err_t err = wm.setCredentials(max_ssid, max_pass);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    std::string read_ssid, read_pass;
    err = wm.getCredentials(read_ssid, read_pass);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(32, read_ssid.length());
    TEST_ASSERT_EQUAL(64, read_pass.length());
    TEST_ASSERT_EQUAL_STRING(max_ssid.c_str(), read_ssid.c_str());
    TEST_ASSERT_EQUAL_STRING(max_pass.c_str(), read_pass.c_str());

    // Persistence across deinit/init
    wm.deinit();
    wm.init();
    TEST_ASSERT_TRUE(wm.isCredentialsValid());
    wm.getCredentials(read_ssid, read_pass);
    TEST_ASSERT_EQUAL_STRING(max_ssid.c_str(), read_ssid.c_str());

    wm.deinit();
}

/**
 * 4. Test NVS memory leak
 */
TEST_CASE("test_nvs_leak", "[memory][nvs]")
{
    printf("\n=== Testing NVS Memory Leak ===\n");
    print_memory("Before NVS init");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    print_memory("After NVS init");
    nvs_flash_deinit();
    print_memory("After NVS deinit");
}

/**
 * 5. Test Validity Flag Persistence
 */
TEST_CASE("test_wifi_valid_flag_persistence", "[wifi][nvs]")
{
    set_memory_leak_threshold(-2000);
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    nvs_flash_erase(); // Ensure clean state
    wm.init();

    wm.setCredentials("ValidSSID", "ValidPass");
    TEST_ASSERT_TRUE(wm.isCredentialsValid());

    wm.deinit();
    wm.init();
    // after reinit should be true
    TEST_ASSERT_TRUE(wm.isCredentialsValid());

    wm.clearCredentials();
    TEST_ASSERT_FALSE(wm.isCredentialsValid());

    wm.deinit();
    wm.init();
    // After re-init, with invlad credintias, if Kconfig has a default SSID, it will be applied and
    // flag set to true
    if (strlen(CONFIG_WIFI_SSID) > 0) {
        TEST_ASSERT_TRUE(wm.isCredentialsValid());
    }
    else {
        TEST_ASSERT_FALSE(wm.isCredentialsValid());
    }

    wm.deinit();
}

/**
 * 6. Test Factory Reset
 */
TEST_CASE("test_wifi_factory_reset", "[wifi][nvs]")
{
    set_memory_leak_threshold(-2000);
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();

    wm.setCredentials("FactorySSID", "FactoryPass");
    TEST_ASSERT_TRUE(wm.isCredentialsValid());

    printf("Calling factoryReset()...\n");
    esp_err_t err = wm.factoryReset();
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_FALSE(wm.isCredentialsValid());
    TEST_ASSERT_EQUAL(WiFiManager::State::INITIALIZED, wm.getState());

    std::string ssid, pass;
    wm.getCredentials(ssid, pass);
    TEST_ASSERT_EQUAL(0, ssid.length());

    wm.deinit();
}

/**
 * 7. Test NVS Auto-repair
 */
TEST_CASE("test_nvs_auto_repair", "[wifi][nvs]")
{
    set_memory_leak_threshold(-2000);
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    nvs_flash_deinit();

    printf("Erasing NVS flash...\n");
    esp_err_t err = nvs_flash_erase();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    printf("Initializing WiFiManager after NVS erase...\n");
    err = wm.init();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    err = wm.setCredentials("RepairSSID", "RepairPass");
    TEST_ASSERT_EQUAL(ESP_OK, err);

    wm.deinit();
}

// ========================================================================
// GROUP 2: LIFECYCLE (INIT/START/STOP)
// ========================================================================

/**
 * 8. Test Singleton Pattern
 */
TEST_CASE("test_singleton_pattern", "[wifi][singleton]")
{
    WiFiManager &instance1 = WiFiManager::instance();
    WiFiManager &instance2 = WiFiManager::instance();
    TEST_ASSERT_EQUAL_PTR(&instance1, &instance2);
}

/**
 * 9. Test Multiple Init Calls
 */
TEST_CASE("test_multiple_init_calls", "[wifi][init]")
{
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    TEST_ASSERT_EQUAL(ESP_OK, wm.init());
    TEST_ASSERT_EQUAL(ESP_OK, wm.init());
    wm.deinit();
}

/**
 * 10. Test State Transitions
 */
TEST_CASE("test_state_transitions", "[wifi][state]")
{
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    WiFiManager::State state = wm.getState();
    TEST_ASSERT(state == WiFiManager::State::INITIALIZED);
    wm.deinit();
}

/**
 * 11. Test WiFi Start/Stop
 */
TEST_CASE("test_wifi_start_stop", "[wifi][state]")
{
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();

    TEST_ASSERT_EQUAL(ESP_OK, wm.start(5000));
    TEST_ASSERT_EQUAL(WiFiManager::State::STARTED, wm.getState());

    TEST_ASSERT_EQUAL(ESP_OK, wm.stop(5000));
    TEST_ASSERT_EQUAL(WiFiManager::State::STOPPED, wm.getState());

    wm.deinit();
}

/**
 * 12. Test Rapid start/stop cycles
 */
TEST_CASE("test_wifi_rapid_start_stop", "[wifi][stress]")
{
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();

    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, wm.start(5000));
        TEST_ASSERT_EQUAL(ESP_OK, wm.stop(5000));
    }
    wm.deinit();
}

/**
 * 13. Test WiFi Queue Spam Robustness
 */
TEST_CASE("test_wifi_spam_robustness", "[wifi][stress]")
{
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    wm.start();

    printf("Sending 100 redundant connect commands...\n");
    wm.setCredentials("StressSSID", "password");
    for (int i = 0; i < 100; i++) {
        wm.connect();
    }

    vTaskDelay(pdMS_TO_TICKS(500));
    wm.deinit();
}

/**
 * 14. Test WiFi API Abuse (Invalid States)
 */
TEST_CASE("test_wifi_api_abuse", "[wifi][error]")
{
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, wm.start(1000));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, wm.connect(1000));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, wm.disconnect(1000));

    wm.init();
    // Connect without start should fail
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, wm.connect(1000));
    wm.deinit();
}

TEST_CASE("test_start_stop_state_validation", "[wifi][state][startstop]")
{
    printf("\n=== Test: START/STOP State Validation ===\n");

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();

    // Test 1: START from UNINITIALIZED (should fail)
    printf("1. START from UNINITIALIZED...\n");
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, wm.start(100));

    // Test 2: Init, then START (should work)
    printf("2. Init -> START...\n");
    TEST_ASSERT_EQUAL(ESP_OK, wm.init());
    TEST_ASSERT_EQUAL(ESP_OK, wm.start(3000));
    TEST_ASSERT_EQUAL(WiFiManager::State::STARTED, wm.getState());

    // Test 3: Redundant START (should succeed immediately)
    printf("3. Redundant START...\n");
    TEST_ASSERT_EQUAL(ESP_OK, wm.start(100));

    // Test 4: STOP from STARTED (should work)
    printf("4. STOP from STARTED...\n");
    TEST_ASSERT_EQUAL(ESP_OK, wm.stop(3000));
    TEST_ASSERT_EQUAL(WiFiManager::State::STOPPED, wm.getState());

    // Test 5: Redundant STOP (should succeed immediately)
    printf("5. Redundant STOP...\n");
    TEST_ASSERT_EQUAL(ESP_OK, wm.stop(100));

    // Test 6: START from STOPPED (should work again)
    printf("6. START from STOPPED...\n");
    TEST_ASSERT_EQUAL(ESP_OK, wm.start(3000));

    // Test 7: Rapid START/STOP cycles
    printf("7. Rapid START/STOP cycles...\n");
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, wm.stop(1000));
        TEST_ASSERT_EQUAL(ESP_OK, wm.start(1000));
    }

    // System should remain stable
    TEST_ASSERT_EQUAL(WiFiManager::State::STARTED, wm.getState());

    // Cleanup
    TEST_ASSERT_EQUAL(ESP_OK, wm.deinit());
}

// ========================================================================
// GROUP 3: CONNECTION (REAL AP)
// ========================================================================

/**
 * 15. Test Real WiFi Connection (Async)
 */
TEST_CASE("test_wifi_connect_real_async", "[wifi][connect][real]")
{
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    wm.start();

    printf("Connecting to %s (Async)...\n", TEST_WIFI_SSID);
    wm.setCredentials(TEST_WIFI_SSID, TEST_WIFI_PASS);
    TEST_ASSERT_EQUAL(ESP_OK, wm.connect());

    int retry = 0;
    while (wm.getState() != WiFiManager::State::CONNECTED_GOT_IP && retry < 150) {
        vTaskDelay(pdMS_TO_TICKS(100));
        retry++;
    }
    TEST_ASSERT_EQUAL(WiFiManager::State::CONNECTED_GOT_IP, wm.getState());
    wm.deinit();
}

/**
 * 16. Test Real WiFi Connection (Sync)
 */
TEST_CASE("test_wifi_connect_real_sync", "[wifi][connect][real]")
{
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    wm.start();

    printf("Connecting to %s (Sync)...\n", TEST_WIFI_SSID);
    wm.setCredentials(TEST_WIFI_SSID, TEST_WIFI_PASS);
    TEST_ASSERT_EQUAL(ESP_OK, wm.connect(10000));
    TEST_ASSERT_EQUAL(WiFiManager::State::CONNECTED_GOT_IP, wm.getState());
    wm.deinit();
}

/**
 * 17. Test WiFi Reconnection (Manual)
 */
TEST_CASE("test_wifi_reconnect_manual", "[wifi][connect][real]")
{
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    wm.start();

    wm.setCredentials(TEST_WIFI_SSID, TEST_WIFI_PASS);
    TEST_ASSERT_EQUAL(ESP_OK, wm.connect(15000));

    printf("Disconnecting via wm.disconnect()...\n");
    wm.disconnect(5000);
    TEST_ASSERT_EQUAL(WiFiManager::State::DISCONNECTED, wm.getState());

    printf("Reconnecting manually...\n");
    TEST_ASSERT_EQUAL(ESP_OK, wm.connect(15000));
    TEST_ASSERT_EQUAL(WiFiManager::State::CONNECTED_GOT_IP, wm.getState());

    wm.deinit();
}

/**
 * 18. Test WiFi Start/Stop (Async)
 */
TEST_CASE("test_wifi_start_stop_async", "[wifi][state]")
{
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();

    printf("Calling start() async...\n");
    TEST_ASSERT_EQUAL(ESP_OK, wm.start());
    int retry = 0;
    while (wm.getState() != WiFiManager::State::STARTED && retry < 50) {
        vTaskDelay(pdMS_TO_TICKS(100));
        retry++;
    }
    TEST_ASSERT_EQUAL(WiFiManager::State::STARTED, wm.getState());

    printf("Calling stop() async...\n");
    TEST_ASSERT_EQUAL(ESP_OK, wm.stop());
    retry = 0;
    while (wm.getState() != WiFiManager::State::STOPPED && retry < 50) {
        vTaskDelay(pdMS_TO_TICKS(100));
        retry++;
    }
    TEST_ASSERT_EQUAL(WiFiManager::State::STOPPED, wm.getState());
    wm.deinit();
}

/**
 * 19. Test WiFi Start/Stop (Sync)
 */
TEST_CASE("test_wifi_start_stop_sync", "[wifi][state]")
{
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();

    printf("Calling start(5000) sync...\n");
    TEST_ASSERT_EQUAL(ESP_OK, wm.start(5000));
    TEST_ASSERT_EQUAL(WiFiManager::State::STARTED, wm.getState());

    printf("Calling stop(5000) sync...\n");
    TEST_ASSERT_EQUAL(ESP_OK, wm.stop(5000));
    TEST_ASSERT_EQUAL(WiFiManager::State::STOPPED, wm.getState());
    wm.deinit();
}

/**
 * 20. Test WiFi with Wrong Password (Sync)
 */
TEST_CASE("test_wifi_connect_wrong_password", "[wifi][connect][real]")
{
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    wm.start();

    printf("Connecting with WRONG password (Sync)...\n");
    wm.setCredentials(TEST_WIFI_SSID, "wrong_password_123");
    esp_err_t err = wm.connect(15000);

    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(WiFiManager::State::ERROR_CREDENTIALS, wm.getState());
    TEST_ASSERT_FALSE(wm.isCredentialsValid());
    wm.deinit();
}

/**
 * 21. Test WiFi with Wrong Password (Async)
 */
TEST_CASE("test_wifi_connect_wrong_password_async", "[wifi][connect][real]")
{
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    wm.start();

    printf("Connecting with WRONG password (Async)...\n");
    wm.setCredentials(TEST_WIFI_SSID, "wrong_password_123");
    wm.connect();

    int retry = 0;
    while (wm.getState() != WiFiManager::State::ERROR_CREDENTIALS && retry < 150) {
        vTaskDelay(pdMS_TO_TICKS(100));
        retry++;
    }

    TEST_ASSERT_EQUAL(WiFiManager::State::ERROR_CREDENTIALS, wm.getState());
    TEST_ASSERT_FALSE(wm.isCredentialsValid());
    wm.deinit();
}

/**
 * 22. Test Connection rollback on timeout
 */
TEST_CASE("test_wifi_connect_rollback", "[wifi][connect]")
{
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    wm.start();

    WiFiManager::State s = wm.getState();
    printf("Final state: %d\n", (int)s);
    wm.setCredentials("NonExistentSSID_Rollback", "password");
    esp_err_t err = wm.connect(1000);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, err);
    s = wm.getState();
    printf("Final state: %d\n", (int)s);

    int retry = 0;
    while (wm.getState() == WiFiManager::State::CONNECTING && retry < 20) {
        vTaskDelay(pdMS_TO_TICKS(100));
        retry++;
    }
    s = wm.getState();
    printf("Final state: %d\n", (int)s);
    TEST_ASSERT_EQUAL(WiFiManager::State::DISCONNECTED, wm.getState());
    wm.deinit();
}

/**
 * 23. Test Start rollback on timeout
 */
TEST_CASE("test_wifi_start_rollback", "[wifi][state]")
{
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();

    WiFiManager::State inicial_state = wm.getState();
    printf("Final state: %d\n", (int)inicial_state);
    esp_err_t err = wm.start(1);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, err);

    vTaskDelay(pdMS_TO_TICKS(500));

    WiFiManager::State final_state = wm.getState();
    printf("Final state: %d\n", (int)final_state);
    TEST_ASSERT(final_state == WiFiManager::State::STOPPED ||
                final_state == WiFiManager::State::INITIALIZED);
    wm.deinit();
}
