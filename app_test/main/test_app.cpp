#include "ota_sender_app.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstring>

static const char *TAG = "TestApp";

TestApp::TestApp()
{
}

void TestApp::init()
{
    ESP_LOGI(TAG, "Initializing TestApp");
    // initis here
}

void TestApp::run()
{
    ESP_LOGI(TAG, "Starting run()");
    // code here
    ESP_LOGI(TAG, "Starting Application Loop");
    while (true) {
        // loop code here
        vTaskDelay(pdMS_TO_TICKS(100)); // non-blocking
    }
}
