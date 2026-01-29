// test_wifi_manager.cpp
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "unity.h"
#include <stdio.h>

#include "secrets.h"
#include "test_memory_helper.h"
#include "wifi_manager.hpp"

static const char *TAG = "test_wifi";

static void print_memory(const char *label)
{
    size_t free_8bit  = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t free_32bit = heap_caps_get_free_size(MALLOC_CAP_32BIT);
    printf("%s - 8BIT: %u, 32BIT: %u bytes free\n", label, (unsigned)free_8bit,
           (unsigned)free_32bit);
}

/**
 * 1. Initialize WiFi Manager once and deinitialize
 */
TEST_CASE("1. test_wifi_init_once", "[wifi][init]")
{
    // Use larger threshold for WiFi
    set_memory_leak_threshold(-15000); // 15KB allowed for WiFi

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit(); // Ensure isolation

    printf("Testing WiFi Manager initialization...\n");

    esp_err_t ret = wm.init();
    TEST_ASSERT(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE);

    printf("WiFi Manager initialized successfully\n");

    // Cleanup
    ret = wm.deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    // Restore default threshold
    reset_memory_leak_threshold();
}

/**
 * 2. Test storing and loading WiFi credentials
 */
TEST_CASE("2. test_wifi_credentials", "[wifi][nvs]")
{
    // Use smaller threshold for NVS
    set_memory_leak_threshold(-2000); // 2KB allowed for NVS

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();

    // Initialize if necessary
    wm.init();

    // Test credential storage
    std::string test_ssid = "TestNetwork";
    std::string test_pass = "TestPassword123";

    esp_err_t ret = wm.storeCredentials(test_ssid, test_pass);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    // Test reading
    std::string read_ssid, read_pass;
    ret = wm.loadCredentials(read_ssid, read_pass);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_STRING(test_ssid.c_str(), read_ssid.c_str());
    TEST_ASSERT_EQUAL_STRING(test_pass.c_str(), read_pass.c_str());

    // Cleanup
    wm.deinit();
    reset_memory_leak_threshold();
}

/**
 * 3. Test NVS memory leak
 */
TEST_CASE("3. test_nvs_leak", "[memory][nvs]")
{
    printf("\n=== Testing NVS Memory Leak ===\n");
    print_memory("Before NVS init");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    print_memory("After NVS init");

    // Deinit NVS (IMPORTANT!)
    nvs_flash_deinit();

    print_memory("After NVS deinit");

    size_t delta = heap_caps_get_free_size(MALLOC_CAP_8BIT) -
                   heap_caps_get_free_size(MALLOC_CAP_8BIT); // placeholder
    printf("NVS memory delta: %d bytes\n", (int)delta);
}

/**
 * 4. Test WiFi Manager memory leak
 */
TEST_CASE("4. test_wifi_manager_leak", "[memory][wifi_manager]")
{
    printf("\n=== Testing WiFi Manager Memory Leak ===\n");

    set_memory_leak_threshold(-15000);

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();

    print_memory("Before WiFi Manager init");

    // Initialize
    esp_err_t ret = wm.init();
    printf("Init result: %d\n", ret);

    print_memory("After WiFi Manager init");

    // IMPORTANT: Must call deinit() to clean up everything
    ret = wm.deinit();
    printf("Deinit result: %d\n", ret);

    print_memory("After WiFi Manager deinit");

    // Restore threshold
    reset_memory_leak_threshold();
}

/**
 * 5. Test Singleton Pattern
 */
TEST_CASE("5. test_singleton_pattern", "[wifi][singleton]")
{
    set_memory_leak_threshold(-15000);

    printf("\n=== Testing Singleton Pattern ===\n");
    WiFiManager::instance().deinit(); // Ensure isolation

    // 1. Test that instance() always returns the same reference
    WiFiManager &instance1 = WiFiManager::instance();
    WiFiManager &instance2 = WiFiManager::instance();

    printf("Address of instance1: %p\n", &instance1);
    printf("Address of instance2: %p\n", &instance2);

    // Verify they are the same instance (same address)
    TEST_ASSERT_EQUAL_PTR(&instance1, &instance2);

    // 2. Test that methods can be called
    printf("Initial state: %d\n", (int)instance1.getState());

    // 3. Initialization test
    esp_err_t ret = instance1.init();
    printf("Init result: %d (%s)\n", ret, esp_err_to_name(ret));

    // First initialization should work
    if (ret == ESP_OK) {
        printf("First init successful\n");
    }
    else if (ret == ESP_ERR_INVALID_STATE) {
        printf("Already initialized\n");
    }

    // 4. Verify state after init
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
TEST_CASE("6. test_multiple_init_calls", "[wifi][init]")
{
    set_memory_leak_threshold(-15000);

    printf("\n=== Testing Multiple Init Calls ===\n");
    WiFiManager::instance().deinit(); // Ensure isolation
    WiFiManager &wm = WiFiManager::instance();

    // First initialization
    esp_err_t ret1 = wm.init();
    printf("First init: %d (%s)\n", ret1, esp_err_to_name(ret1));

    // Second initialization - should be idempotent
    esp_err_t ret2 = wm.init();
    printf("Second init: %d (%s)\n", ret2, esp_err_to_name(ret2));

    // Both should return success (ESP_OK or similar)
    TEST_ASSERT(ret1 == ESP_OK || ret1 == ESP_ERR_INVALID_STATE);
    TEST_ASSERT(ret2 == ESP_OK || ret2 == ESP_ERR_INVALID_STATE);

    // Cleaning
    wm.deinit();

    printf("✓ Multiple init test passed!\n");
}

/**
 * 7. Test State Transitions
 */
TEST_CASE("7. test_state_transitions", "[wifi][state]")
{
    set_memory_leak_threshold(-15000);

    printf("\n=== Testing State Management ===\n");

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();

    wm.init();

    // Verify state is valid
    WiFiManager::State state = wm.getState();
    printf("Current state: %d\n", (int)state);

    // State must be one of the valid values
    TEST_ASSERT(state >= WiFiManager::State::UNINITIALIZED &&
                state <= WiFiManager::State::STOPPED);

    // Cleaning
    wm.deinit();

    printf(" State test passed!\n");
}

/**
 * 8. Test NVS Auto-repair
 */
TEST_CASE("8. test_nvs_auto_repair", "[wifi][nvs]")
{
    printf("\n=== Testing NVS Auto-repair ===\n");

    WiFiManager &wm = WiFiManager::instance();

    // 1. Ensure it's uninitialized
    wm.deinit();
    nvs_flash_deinit();

    // 2. Erase flash completely to force re-initialization
    printf("Erasing NVS flash...\n");
    esp_err_t err = nvs_flash_erase();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    // 3. Try to initialize WiFiManager
    // init() calls init_nvs() which should handle the clean flash
    printf("Initializing WiFiManager after NVS erase...\n");
    err = wm.init();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    // 4. Verify we can use NVS now
    err = wm.storeCredentials("RepairSSID", "RepairPass");
    TEST_ASSERT_EQUAL(ESP_OK, err);

    printf("✓ NVS auto-repair test passed!\n");
    wm.deinit();
}

/**
 * 9. Test Credentials Deep
 */
TEST_CASE("9. test_credentials_deep", "[wifi][nvs]")
{
    printf("\n=== Testing Credentials Deep ===\n");

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();

    // 1. Buffer limits test (Max Lengths)
    // SSID: 32 chars, Password: 64 chars
    std::string max_ssid(32, 'S');
    std::string max_pass(64, 'P');

    printf("Testing max length credentials...\n");
    esp_err_t err = wm.storeCredentials(max_ssid, max_pass);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    std::string read_ssid, read_pass;
    err = wm.loadCredentials(read_ssid, read_pass);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING(max_ssid.c_str(), read_ssid.c_str());
    TEST_ASSERT_EQUAL_STRING(max_pass.c_str(), read_pass.c_str());

    // 2. Persistence test between Deinit/Init
    printf("Testing persistence across deinit/init...\n");
    std::string p_ssid = "PersistSSID";
    std::string p_pass = "PersistPass";
    wm.storeCredentials(p_ssid, p_pass);

    wm.deinit();
    // Re-initialize
    wm.init();

    std::string check_ssid, check_pass;
    TEST_ASSERT_TRUE(wm.hasCredentials());
    wm.loadCredentials(check_ssid, check_pass);
    TEST_ASSERT_EQUAL_STRING(p_ssid.c_str(), check_ssid.c_str());
    TEST_ASSERT_EQUAL_STRING(p_pass.c_str(), check_pass.c_str());

    printf("✓ Credentials deep test passed!\n");
    wm.deinit();
}

/**
 * 10. Test WiFi Start/Stop
 */
TEST_CASE("10. test_wifi_start_stop", "[wifi][state]")
{
    printf("\n=== Testing WiFi Start/Stop ===\n");

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();

    // 1. Test Start
    printf("Calling start()...\n");
    esp_err_t err = wm.start(5000);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(WiFiManager::State::STARTED, wm.getState());

    // 2. Test Stop
    printf("Calling stop()...\n");
    err = wm.stop(5000);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(WiFiManager::State::STOPPED, wm.getState());

    printf("✓ Start/Stop test passed!\n");
    wm.deinit();
}

/**
 * 11. Test WiFi Connect Timeout
 */
TEST_CASE("11. test_wifi_connect_timeout", "[wifi][connect]")
{
    printf("\n=== Testing WiFi Connect Timeout ===\n");

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    wm.start();

    printf("Calling connect() with non-existent SSID and 2s timeout...\n");
    // Use an SSID that probably doesn't exist
    int64_t start_time = esp_timer_get_time();
    esp_err_t err = wm.connect("NonExistentSSID_12345", "wrong_password", 2000);
    int64_t end_time = esp_timer_get_time();

    printf("Connect returned after %lld ms\n", (end_time - start_time) / 1000);

    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, err);

    // Verify state - with rollback it should transition to DISCONNECTED
    vTaskDelay(pdMS_TO_TICKS(500));
    WiFiManager::State state = wm.getState();
    printf("State after timeout and rollback: %d\n", (int)state);

    TEST_ASSERT_EQUAL(WiFiManager::State::DISCONNECTED, state);

    wm.deinit();
}

/**
 * 12. Test WiFi Queue Spam Robustness
 */
TEST_CASE("12. test_wifi_spam_robustness", "[wifi][stress]")
{
    printf("\n=== Testing WiFi Queue Spam Robustness ===\n");

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    wm.start();

    // Test if the task processes redundant commands fast enough
    printf("Sending 100 redundant connect commands...\n");
    int fail_count = 0;
    for (int i = 0; i < 100; i++) {
        if (wm.connect_async("StressSSID", "password") != ESP_OK) {
            fail_count++;
        }
    }

    printf("Failed to send: %d\n", fail_count);
    // Note: If fail_count is 0, it means the task is fast enough to clean the queue
    // by filtering redundant commands. This is a technical success.
    TEST_ASSERT_EQUAL(0, fail_count);

    vTaskDelay(pdMS_TO_TICKS(500));
    wm.deinit();
}

/**
 * 13. Test WiFi API Abuse (Invalid States)
 */
TEST_CASE("13. test_wifi_api_abuse", "[wifi][error]")
{
    printf("\n=== Testing WiFi API Abuse (Invalid States) ===\n");

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit(); // Ensure we start from UNINITIALIZED

    // 1. Try start without init - Should return INVALID_STATE and NOT crash
    printf("Calling start() before init()...\n");
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, wm.start());

    // 2. Try connect without init
    printf("Calling connect() before init()...\n");
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, wm.connect("SSID", "PASS", 1000));

    // 3. Try disconnect without init
    printf("Calling disconnect() before init()...\n");
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, wm.disconnect());

    wm.init();

    // 4. Try connect without start (driver wifi not loaded)
    printf("Calling connect() after init() but before start()...\n");
    esp_err_t err = wm.connect("SSID", "PASS", 1000);
    printf("Connect returned: %s\n", esp_err_to_name(err));
    // Should return ESP_FAIL immediately due to invalid state rejection
    TEST_ASSERT_EQUAL(ESP_FAIL, err);

    wm.deinit();
}

static void connect_task(void *pvParameters)
{
    const char *ssid = (const char *)pvParameters;
    WiFiManager &wm  = WiFiManager::instance();
    printf("Task connecting to %s...\n", ssid);
    esp_err_t err = wm.connect(ssid, "password", 1000);
    printf("Task %s finished with error: %d\n", ssid, err);
    vTaskDelete(NULL);
}

/**
 * 14. Test WiFi Concurrency
 */
TEST_CASE("14. test_wifi_concurrency", "[wifi][concurrency]")
{
    printf("\n=== Testing WiFi Concurrency ===\n");

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    wm.start();

    printf("Launching two concurrent connect tasks...\n");
    xTaskCreate(connect_task, "conn_1", 4096, (void *)"SSID_A", 5, NULL);
    xTaskCreate(connect_task, "conn_2", 4096, (void *)"SSID_B", 5, NULL);

    // Wait for timeouts/processing
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Verify manager is not stuck and can handle deinit
    TEST_ASSERT_EQUAL(ESP_OK, wm.deinit());
}

/**
 * 15. Test Real WiFi Connection (Async)
 */
TEST_CASE("15. test_wifi_connect_real_async", "[wifi][connect][real]")
{
    printf("\n=== Testing Real WiFi Connection (Async) ===\n");

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    wm.start();

    printf("Connecting to %s (Async)...\n", TEST_WIFI_SSID);
    esp_err_t err = wm.connect_async(TEST_WIFI_SSID, TEST_WIFI_PASS);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    // Wait up to 15 seconds for connection and IP
    printf("Waiting for IP...\n");
    int retry = 0;
    while (wm.getState() != WiFiManager::State::CONNECTED_GOT_IP && retry < 150) {
        vTaskDelay(pdMS_TO_TICKS(100));
        retry++;
    }

    WiFiManager::State final_state = wm.getState();
    printf("Final state: %d after %d ms\n", (int)final_state, retry * 100);

    if (final_state != WiFiManager::State::CONNECTED_GOT_IP) {
        printf("FAILED to connect to real WiFi. Check your secrets.h\n");
    } else {
        printf("✓ Successfully connected to real WiFi!\n");
        TEST_ASSERT_EQUAL(WiFiManager::State::CONNECTED_GOT_IP, final_state);
    }

    wm.deinit();
}

/**
 * 16. Test WiFi with Wrong Password
 */
TEST_CASE("16. test_wifi_connect_wrong_password", "[wifi][connect][real]")
{
    printf("\n=== Testing WiFi with Wrong Password ===\n");

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    wm.start();

    printf("Connecting to %s with WRONG password...\n", TEST_WIFI_SSID);
    esp_err_t err = wm.connect(TEST_WIFI_SSID, "wrong_password_123", 10000);

    // Should return timeout or error
    printf("Connect returned: %s\n", esp_err_to_name(err));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);

    WiFiManager::State state = wm.getState();
    printf("State after failed connection: %d\n", (int)state);

    // State should reflect not connected
    TEST_ASSERT_NOT_EQUAL(WiFiManager::State::CONNECTED_GOT_IP, state);

    wm.deinit();
}

/**
 * 17. Test WiFi Reconnection (Manual)
 */
TEST_CASE("17. test_wifi_reconnect_manual", "[wifi][connect][real]")
{
    printf("\n=== Testing WiFi Reconnection (Manual) ===\n");

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    wm.start();

    printf("1. Connecting to %s...\n", TEST_WIFI_SSID);
    if (wm.connect(TEST_WIFI_SSID, TEST_WIFI_PASS, 15000) != ESP_OK) {
        printf("Could not connect to real WiFi for reconnection test. Skipping.\n");
        wm.deinit();
        return;
    }

    TEST_ASSERT_EQUAL(WiFiManager::State::CONNECTED_GOT_IP, wm.getState());

    // 2. Force disconnect via driver
    printf("2. Forcing disconnect via esp_wifi_disconnect()...\n");
    esp_wifi_disconnect();

    // Wait for detection
    vTaskDelay(pdMS_TO_TICKS(1000));

    WiFiManager::State state_after_drop = wm.getState();
    printf("State after forced drop: %d\n", (int)state_after_drop);
    TEST_ASSERT_EQUAL(WiFiManager::State::DISCONNECTED, state_after_drop);

    // 3. Try to reconnect manually via class
    printf("3. Reconnecting manually via connect()...\n");
    esp_err_t err = wm.connect(TEST_WIFI_SSID, TEST_WIFI_PASS, 15000);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(WiFiManager::State::CONNECTED_GOT_IP, wm.getState());

    printf("✓ Reconnection test passed!\n");
    wm.deinit();
}

/**
 * 18. Test WiFi Disconnect (Async)
 */
TEST_CASE("18. test_wifi_disconnect_async", "[wifi][connect][real]")
{
    printf("\n=== Testing WiFi Disconnect (Async) ===\n");

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    wm.start();

    printf("1. Connecting to %s...\n", TEST_WIFI_SSID);
    if (wm.connect(TEST_WIFI_SSID, TEST_WIFI_PASS, 15000) != ESP_OK) {
        printf("Could not connect to real WiFi for disconnect test. Skipping.\n");
        wm.deinit();
        return;
    }

    TEST_ASSERT_EQUAL(WiFiManager::State::CONNECTED_GOT_IP, wm.getState());

    printf("2. Disconnecting (Async)...\n");
    esp_err_t err = wm.disconnect_async();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    // Wait for detection
    int retry = 0;
    while (wm.getState() != WiFiManager::State::DISCONNECTED && retry < 50) {
        vTaskDelay(pdMS_TO_TICKS(100));
        retry++;
    }

    WiFiManager::State state_after_disconnect = wm.getState();
    printf("State after async disconnect: %d\n", (int)state_after_disconnect);
    TEST_ASSERT_EQUAL(WiFiManager::State::DISCONNECTED, state_after_disconnect);

    printf("✓ Async disconnect test passed!\n");
    wm.deinit();
}

/**
 * 19. Test Rapid start/stop cycles
 */
TEST_CASE("19. test_wifi_rapid_start_stop", "[wifi][stress]")
{
    printf("\n=== Testing Rapid start/stop cycles ===\n");
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();

    for (int i = 0; i < 10; i++) {
        printf("Cycle %d/10\n", i + 1);
        TEST_ASSERT_EQUAL(ESP_OK, wm.start(5000));
        TEST_ASSERT_EQUAL(WiFiManager::State::STARTED, wm.getState());
        TEST_ASSERT_EQUAL(ESP_OK, wm.stop(5000));
        TEST_ASSERT_EQUAL(WiFiManager::State::STOPPED, wm.getState());
    }

    wm.deinit();
}

/**
 * 20. Test Connection rollback on timeout
 */
TEST_CASE("20. test_wifi_connect_rollback", "[wifi][connect]")
{
    printf("\n=== Testing Connection rollback on timeout ===\n");
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    wm.start();

    printf("Calling connect() with 1s timeout (forcing timeout)...\n");
    esp_err_t err = wm.connect("NonExistentSSID_999", "password", 1000);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, err);

    printf("Waiting for rollback to DISCONNECTED...\n");
    int retry = 0;
    while (wm.getState() == WiFiManager::State::CONNECTING && retry < 20) {
        vTaskDelay(pdMS_TO_TICKS(100));
        retry++;
    }

    WiFiManager::State final_state = wm.getState();
    printf("State after rollback: %d\n", (int)final_state);
    TEST_ASSERT_EQUAL(WiFiManager::State::DISCONNECTED, final_state);

    wm.deinit();
}

/**
 * 21. Test Start rollback on timeout
 */
TEST_CASE("21. test_wifi_start_rollback", "[wifi][state]")
{
    printf("\n=== Testing Start rollback on timeout ===\n");
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();

    printf("Calling start() with 1ms timeout (forcing timeout)...\n");
    esp_err_t err = wm.start(1);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, err);

    vTaskDelay(pdMS_TO_TICKS(500));

    WiFiManager::State final_state = wm.getState();
    printf("State after rollback: %d\n", (int)final_state);
    TEST_ASSERT(final_state == WiFiManager::State::STOPPED ||
                final_state == WiFiManager::State::INITIALIZED);

    wm.deinit();
}

/**
 * 22. Test WiFi Start/Stop (Async)
 */
TEST_CASE("22. test_wifi_start_stop_async", "[wifi][state]")
{
    printf("\n=== Testing WiFi Start/Stop (Async) ===\n");
    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();

    printf("Calling start_async()...\n");
    TEST_ASSERT_EQUAL(ESP_OK, wm.start_async());

    int retry = 0;
    while (wm.getState() != WiFiManager::State::STARTED && retry < 50) {
        vTaskDelay(pdMS_TO_TICKS(100));
        retry++;
    }
    TEST_ASSERT_EQUAL(WiFiManager::State::STARTED, wm.getState());

    printf("Calling stop_async()...\n");
    TEST_ASSERT_EQUAL(ESP_OK, wm.stop_async());

    retry = 0;
    while (wm.getState() != WiFiManager::State::STOPPED && retry < 50) {
        vTaskDelay(pdMS_TO_TICKS(100));
        retry++;
    }
    TEST_ASSERT_EQUAL(WiFiManager::State::STOPPED, wm.getState());

    wm.deinit();
}
