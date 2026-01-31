// components/wifi_manager/test/main/test_wifi_manager_internal.cpp
#include "esp_timer.h"
#include "esp_wifi.h"
#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>

#include "test_memory_helper.h"
#include "test_wifi_manager_accessor.hpp"

#ifdef UNIT_TEST

/**
 * @brief Test: Queue capacity and behavior
 */
TEST_CASE("test_internal_queue_behavior", "[wifi][internal][stress]")
{
    set_memory_leak_threshold(-2000);
    printf("\n=== Test: Queue Behaviors ===\n");

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();

    WiFiManagerTestAccessor accessor(wm);

    const int COMMANDS_TO_SEND = 15;
    int successful_sends = 0;
    for (int i = 0; i < COMMANDS_TO_SEND; i++) {
        if (accessor.test_sendStartCommand(true) == ESP_OK) {
            successful_sends++;
        }
    }

    TEST_ASSERT_EQUAL(COMMANDS_TO_SEND, successful_sends);
    vTaskDelay(pdMS_TO_TICKS(100));
    wm.deinit();
}

/**
 * @brief Test: No reconnection if invalid
 */
TEST_CASE("test_internal_no_reconnect_if_invalid", "[wifi][internal][reconnect]")
{
    set_memory_leak_threshold(-2000);
    printf("\n=== Test: No Reconnection if Invalid ===\n");

    WiFiManager &wm = WiFiManager::instance();
    wm.deinit();
    wm.init();
    wm.start(5000);

    WiFiManagerTestAccessor accessor(wm);

    wm.clearCredentials();
    TEST_ASSERT_FALSE(wm.isCredentialsValid());

    // Simulate NO_AP_FOUND (Usually retries if valid)
    accessor.test_simulateDisconnect(WIFI_REASON_NO_AP_FOUND);
    vTaskDelay(pdMS_TO_TICKS(100));

    // Should NOT be in WAITING_RECONNECT
    TEST_ASSERT_NOT_EQUAL(WiFiManager::State::WAITING_RECONNECT, wm.getState());

    wm.deinit();
}

#endif // UNIT_TEST
