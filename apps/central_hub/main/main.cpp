#include "central_hub_app.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "central_hub";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Starting Central Hub application");
    auto& app = CentralHubApp::instance();
    app.init();
    app.run();
}
