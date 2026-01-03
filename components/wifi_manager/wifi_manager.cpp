#include "wifi_manager.hpp"
#include "common_types.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include <cstring>

const char *TAG = "WifiManager";

// Internal WiFi command queue commands
enum class WifiCmd : uint8_t
{
    START,
    CONNECT,
    DISCONNECT,
    STOP,
};

// Singleton implementation - Meyer's pattern (thread-safe in C++11+)
WifiManager &WifiManager::instance()
{
    static WifiManager inst;
    return inst;
}

// Initialize WiFi stack and event system
esp_err_t WifiManager::init()
{
    // ⚡ THREAD SAFETY: Create mutex first to protect initialization
    init_mutex_ = xSemaphoreCreateMutex();
    if (init_mutex_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Acquire mutex to prevent concurrent initialization
    if (xSemaphoreTake(init_mutex_, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }

    // Check if already initialized (idempotent operation)
    if (initialized_) {
        xSemaphoreGive(init_mutex_);
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing network stack...");

    // Initialize network stack
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Failed to initialize TCP/IP stack");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG,
                        "Failed to create event loop");
    if (esp_netif_create_default_wifi_sta() == nullptr) {
        xSemaphoreGive(init_mutex_); // Remember to release mutex on error!
        return ESP_FAIL;
    }

    // Register event handlers
    ESP_RETURN_ON_ERROR(esp_event_handler_register(APP_WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                   &WifiManager::cmd_event_handler, this),
                        TAG, "Failed to register event handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                   &WifiManager::event_handler, this),
                        TAG, "Failed to register event handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                   ip_event_handler, nullptr),
                        TAG, "Failed to register IP event handler");

    // Initialize WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "Failed to initialize WiFi stack");

    // Create command queue (critical resource for task communication)
    cmd_queue_ = xQueueCreate(8, sizeof(WifiCmd));
    if (cmd_queue_ == nullptr) {
        xSemaphoreGive(init_mutex_);
        return ESP_ERR_NO_MEM;
    }

    initialized_ = true;
    xSemaphoreGive(init_mutex_); // Release mutex after successful initialization
    ESP_LOGI(TAG, "WiFi stack initialized");
    return ESP_OK;
}

// Start WiFi manager task
void WifiManager::start()
{
    // Pre-check: Ensure init() was called first
    if (!initialized_) {
        ESP_LOGE(TAG, "WiFi not initialized. Call init() first.");
        return;
    }

    // Try to acquire mutex with timeout (avoid deadlock)
    if (xSemaphoreTake(init_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Could not acquire mutex");
        return;
    }

    // Check if task is already running (prevent duplicate tasks)
    if (task_handle_ != nullptr) {
        ESP_LOGW(TAG, "WiFi task already running");
        xSemaphoreGive(init_mutex_);
        return;
    }

    // Create FreeRTOS task for WiFi management
    BaseType_t result =
        xTaskCreate(&WifiManager::task, "wifi_manager", 4096, this, 5, &task_handle_);

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create WiFi task");
        task_handle_ = nullptr;
    }
    else {
        ESP_LOGI(TAG, "WiFi task started (ID: %p)", task_handle_);
    }

    xSemaphoreGive(init_mutex_); // Always release mutex
}

// Handle WiFi events and translate to app events
void WifiManager::event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    // Translate ESP-IDF WiFi events to application-specific events
    switch (id) {
    case WIFI_EVENT_STA_START:
        esp_event_post(APP_WIFI_EVENT, WIFI_EVT_STARTED, nullptr, 0, portMAX_DELAY);
        ESP_LOGI(TAG, "WiFi started");
        break;
    case WIFI_EVENT_STA_CONNECTED:
        esp_event_post(APP_WIFI_EVENT, WIFI_EVT_CONNECTED, nullptr, 0, portMAX_DELAY);
        ESP_LOGI(TAG, "WiFi connected");
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        esp_event_post(APP_WIFI_EVENT, WIFI_EVT_DISCONNECTED, nullptr, 0, portMAX_DELAY);
        ESP_LOGI(TAG, "WiFi disconnected");
        break;
    case WIFI_EVENT_STA_STOP:
        esp_event_post(APP_WIFI_EVENT, WIFI_EVT_STOPPED, nullptr, 0, portMAX_DELAY);
        ESP_LOGI(TAG, "WiFi stopped");
        break;
    default:
        break;
    }
}

// Convert app commands to internal queue commands
void WifiManager::cmd_event_handler(void *arg,
                                    esp_event_base_t base,
                                    int32_t id,
                                    void *data)
{
    auto *self = static_cast<WifiManager *>(arg);

    // SAFETY CHECK: Verify queue exists before using it
    if (self->cmd_queue_ == nullptr) {
        ESP_LOGW(TAG, "Command queue not initialized");
        return;
    }

    WifiCmd cmd;

    // Map application commands to internal commands
    switch (id) {
    case WIFI_CMD_START:
        cmd = WifiCmd::START;
        break;
    case WIFI_CMD_CONNECT:
        cmd = WifiCmd::CONNECT;
        break;
    case WIFI_CMD_DISCONNECT:
        cmd = WifiCmd::DISCONNECT;
        break;
    case WIFI_CMD_STOP:
        cmd = WifiCmd::STOP;
        break;
    default:
        return; // Unknown command (silently ignore)
    }

    // Send command to task via queue (thread-safe communication)
    xQueueSend(self->cmd_queue_, &cmd, portMAX_DELAY);
}

// WiFi management task - processes commands from queue
void WifiManager::task(void *arg)
{
    auto *self = static_cast<WifiManager *>(arg);

    // CRITICAL: Check if queue was created before using it
    // This prevents crashes if task starts before init() completes
    if (self->cmd_queue_ == nullptr) {
        ESP_LOGE(TAG, "Task started before queue creation");
        vTaskDelete(nullptr); // Self-destruct if queue doesn't exist
        return;
    }

    WifiCmd cmd;

    // Main task loop - runs forever (or until deleted)
    while (true) {
        // Block indefinitely waiting for commands (0% CPU when idle)
        if (xQueueReceive(self->cmd_queue_, &cmd, portMAX_DELAY)) {
            // Execute WiFi commands based on queue message
            switch (cmd) {
            case WifiCmd::START:
                esp_wifi_set_mode(WIFI_MODE_STA);
                esp_wifi_start();
                break;
            case WifiCmd::CONNECT:
                esp_wifi_connect();
                break;
            case WifiCmd::DISCONNECT:
                esp_wifi_disconnect();
                break;
            case WifiCmd::STOP:
                esp_wifi_stop();
                break;
            default:
                break;
            }
        }
        // Note: No timeout used - task sleeps indefinitely when queue empty
        // Consider adding timeout for periodic maintenance/heartbeat
    }
}

// Handle IP events (when station gets IP)
void WifiManager::ip_event_handler(void *arg,
                                   esp_event_base_t base,
                                   int32_t id,
                                   void *data)
{
    if (id == IP_EVENT_STA_GOT_IP) {
        // Extract and log IP information from event data
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        char ip_str[16];

        // Format and log IP address
        esp_ip4addr_ntoa(&event->ip_info.ip, ip_str, sizeof(ip_str));
        ESP_LOGI(TAG, "Got IP address: %s", ip_str);

        // Format and log gateway address (reusing buffer)
        esp_ip4addr_ntoa(&event->ip_info.gw, ip_str, sizeof(ip_str));
        ESP_LOGI(TAG, "Gateway: %s", ip_str);

        // Notify application that IP was acquired
        esp_event_post(APP_WIFI_EVENT, WIFI_EVT_GOT_IP, nullptr, 0, portMAX_DELAY);
    }
}