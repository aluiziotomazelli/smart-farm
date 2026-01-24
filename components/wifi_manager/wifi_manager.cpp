#include "wifi_manager.hpp"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include <cstring>

static const char *TAG = "WiFiManager";

// =================================================================================================
// Singleton and Constructor/Destructor
// =================================================================================================

WiFiManager &WiFiManager::instance()
{
    static WiFiManager instance;
    return instance;
}

WiFiManager::WiFiManager()
    : task_handle_(nullptr)
    , command_queue_(nullptr)
    , wifi_event_group_(nullptr)
    , current_state_(State::UNINITIALIZED)
{
    state_mutex_ = xSemaphoreCreateMutex();
}

WiFiManager::~WiFiManager()
{
    if (task_handle_ != nullptr) {
        vTaskDelete(task_handle_);
    }
    if (command_queue_ != nullptr) {
        vQueueDelete(command_queue_);
    }
    if (wifi_event_group_ != nullptr) {
        vEventGroupDelete(wifi_event_group_);
    }
    if (state_mutex_ != nullptr) {
        vSemaphoreDelete(state_mutex_);
    }
}

// =================================================================================================
// Public API
// =================================================================================================

esp_err_t WiFiManager::init()
{
    if (getState() != State::UNINITIALIZED) {
        ESP_LOGI(TAG, "Already initialized.");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing network stack...");
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    if (esp_netif_create_default_wifi_sta() == nullptr) {
        ESP_LOGE(TAG, "Failed to create default STA netif");
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &WiFiManager::wifiEventHandler, this, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, ESP_EVENT_ANY_ID, &WiFiManager::ipEventHandler, this, nullptr));

    command_queue_    = xQueueCreate(10, sizeof(Command));
    wifi_event_group_ = xEventGroupCreate();
    if (!command_queue_ || !wifi_event_group_) {
        ESP_LOGE(TAG, "Failed to create queue or event group");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t task_created =
        xTaskCreate(wifiTask, "wifi_task", 4096, this, 5, &task_handle_);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create wifi task");
        return ESP_ERR_NO_MEM;
    }

    current_state_ = State::INITIALIZED;
    ESP_LOGI(TAG, "WiFi Manager initialized.");
    return ESP_OK;
}

esp_err_t WiFiManager::start(uint32_t timeout_ms)
{
    ESP_LOGI(TAG, "API: Requesting to start WiFi...");
    Command cmd = {.id = CommandId::START};

    xEventGroupClearBits(wifi_event_group_, STARTED_BIT);
    if (sendCommand(cmd, false) != ESP_OK) {
        return ESP_FAIL;
    }

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group_, STARTED_BIT, pdTRUE,
                                           pdFALSE, pdMS_TO_TICKS(timeout_ms));

    if (bits & STARTED_BIT) {
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t WiFiManager::stop(uint32_t timeout_ms)
{
    ESP_LOGI(TAG, "API: Requesting to stop WiFi...");
    Command cmd = {.id = CommandId::STOP};

    xEventGroupClearBits(wifi_event_group_, STOPPED_BIT);
    if (sendCommand(cmd, false) != ESP_OK) {
        return ESP_FAIL;
    }

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group_, STOPPED_BIT, pdTRUE,
                                           pdFALSE, pdMS_TO_TICKS(timeout_ms));

    if (bits & STOPPED_BIT) {
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t WiFiManager::connect(const std::string &ssid,
                               const std::string &password,
                               uint32_t timeout_ms)
{
    ESP_LOGI(TAG, "API: Requesting to connect (sync)...");
    Command cmd = {.id = CommandId::CONNECT, .ssid = ssid, .password = password};

    xEventGroupClearBits(wifi_event_group_, CONNECTED_BIT | CONNECT_FAILED_BIT);
    if (sendCommand(cmd, false) != ESP_OK) {
        return ESP_FAIL;
    }

    EventBits_t bits =
        xEventGroupWaitBits(wifi_event_group_, CONNECTED_BIT | CONNECT_FAILED_BIT, pdTRUE,
                            pdFALSE, pdMS_TO_TICKS(timeout_ms));

    if (bits & CONNECTED_BIT) {
        return ESP_OK;
    }
    else if (bits & CONNECT_FAILED_BIT) {
        return ESP_FAIL;
    }
    else {
        return ESP_ERR_TIMEOUT;
    }
}

esp_err_t WiFiManager::connect_async(const std::string &ssid, const std::string &password)
{
    ESP_LOGI(TAG, "API: Requesting to connect (async)...");
    Command cmd = {.id = CommandId::CONNECT, .ssid = ssid, .password = password};
    return sendCommand(cmd, true);
}

esp_err_t WiFiManager::disconnect(uint32_t timeout_ms)
{
    ESP_LOGI(TAG, "API: Requesting to disconnect...");
    Command cmd = {.id = CommandId::DISCONNECT};

    xEventGroupClearBits(wifi_event_group_, DISCONNECTED_BIT);
    if (sendCommand(cmd, false) != ESP_OK) {
        return ESP_FAIL;
    }

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group_, DISCONNECTED_BIT, pdTRUE,
                                           pdFALSE, pdMS_TO_TICKS(timeout_ms));

    if (bits & DISCONNECTED_BIT) {
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

WiFiManager::State WiFiManager::getState() const
{
    xSemaphoreTake(state_mutex_, portMAX_DELAY);
    State state = current_state_;
    xSemaphoreGive(state_mutex_);
    return state;
}

// =================================================================================================
// NVS Functions
// =================================================================================================

esp_err_t WiFiManager::storeCredentials(const std::string &ssid,
                                        const std::string &password)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("wifi_manager", NVS_READWRITE, &h);
    if (err != ESP_OK)
        return err;

    err = nvs_set_str(h, "ssid", ssid.c_str());
    if (err == ESP_OK) {
        err = nvs_set_str(h, "pass", password.c_str());
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t WiFiManager::loadCredentials(std::string &ssid, std::string &password)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("wifi_manager", NVS_READONLY, &h);
    if (err != ESP_OK)
        return err;

    char ssid_buf[33] = {0};
    char pass_buf[65] = {0};
    size_t ssid_len   = sizeof(ssid_buf);
    size_t pass_len   = sizeof(pass_buf);

    err = nvs_get_str(h, "ssid", ssid_buf, &ssid_len);
    if (err == ESP_OK) {
        err = nvs_get_str(h, "pass", pass_buf, &pass_len);
    }
    nvs_close(h);

    if (err == ESP_OK) {
        ssid     = ssid_buf;
        password = pass_buf;
    }
    return err;
}

bool WiFiManager::hasCredentials()
{
    std::string ssid, pass;
    return loadCredentials(ssid, pass) == ESP_OK && !ssid.empty();
}

// =================================================================================================
// Internal Implementation
// =================================================================================================

esp_err_t WiFiManager::sendCommand(const Command &cmd, bool is_async)
{
    if (getState() == State::UNINITIALIZED) {
        return ESP_ERR_INVALID_STATE;
    }

    TickType_t timeout = is_async ? 0 : portMAX_DELAY;
    if (xQueueSend(command_queue_, &cmd, timeout) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send command to queue (full?)");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void WiFiManager::wifiEventHandler(void *arg,
                                   esp_event_base_t base,
                                   int32_t id,
                                   void *data)

{
    WiFiManager *self = static_cast<WiFiManager *>(arg);
    Command cmd       = {.id = CommandId::HANDLE_EVENT_WIFI, .event_id = id};
    xQueueSendFromISR(self->command_queue_, &cmd, nullptr);
}

void WiFiManager::ipEventHandler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    WiFiManager *self = static_cast<WiFiManager *>(arg);
    Command cmd       = {.id = CommandId::HANDLE_EVENT_IP, .event_id = id};
    xQueueSendFromISR(self->command_queue_, &cmd, nullptr);
}

void WiFiManager::wifiTask(void *pvParameters)
{
    WiFiManager *self = static_cast<WiFiManager *>(pvParameters);
    Command cmd;

    for (;;) {
        if (xQueueReceive(self->command_queue_, &cmd, portMAX_DELAY) == pdTRUE) {
            // State is locked only during modification
            xSemaphoreTake(self->state_mutex_, portMAX_DELAY);

            switch (cmd.id) {
            case CommandId::START:
                self->current_state_ = State::STARTING;
                esp_wifi_set_mode(WIFI_MODE_STA);
                esp_wifi_start();
                break;
            case CommandId::STOP:
                self->current_state_ = State::STOPPING;
                esp_wifi_stop();
                break;
            case CommandId::CONNECT:
                self->current_state_ = State::CONNECTING;
                {
                    wifi_config_t wifi_config = {};
                    strncpy((char *)wifi_config.sta.ssid, cmd.ssid.c_str(),
                            sizeof(wifi_config.sta.ssid) - 1);
                    strncpy((char *)wifi_config.sta.password, cmd.password.c_str(),
                            sizeof(wifi_config.sta.password) - 1);
                    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
                    esp_wifi_connect();
                }
                break;
            case CommandId::DISCONNECT:
                self->current_state_ = State::DISCONNECTING;
                esp_wifi_disconnect();
                break;

            case CommandId::HANDLE_EVENT_WIFI:
                switch (cmd.event_id) {
                case WIFI_EVENT_STA_START:
                    ESP_LOGI(TAG, "Task Event: STA_START");
                    self->current_state_ = State::STARTED;
                    xEventGroupSetBits(self->wifi_event_group_, STARTED_BIT);
                    break;
                case WIFI_EVENT_STA_STOP:
                    ESP_LOGI(TAG, "Task Event: STA_STOP");
                    self->current_state_ = State::STOPPED;
                    xEventGroupSetBits(self->wifi_event_group_, STOPPED_BIT);
                    break;
                case WIFI_EVENT_STA_CONNECTED:
                    ESP_LOGI(TAG, "Task Event: STA_CONNECTED");
                    self->current_state_ = State::CONNECTED_NO_IP;
                    break;
                case WIFI_EVENT_STA_DISCONNECTED:
                    ESP_LOGI(TAG, "Task Event: STA_DISCONNECTED");
                    self->current_state_ = State::DISCONNECTED;
                    xEventGroupSetBits(self->wifi_event_group_,
                                       DISCONNECTED_BIT | CONNECT_FAILED_BIT);
                    break;
                }
                break;

            case CommandId::HANDLE_EVENT_IP:
                if (cmd.event_id == IP_EVENT_STA_GOT_IP) {
                    ESP_LOGI(TAG, "Task Event: GOT_IP");
                    self->current_state_ = State::CONNECTED_GOT_IP;
                    xEventGroupSetBits(self->wifi_event_group_, CONNECTED_BIT);
                }
                break;
            }
            xSemaphoreGive(self->state_mutex_);
        }
    }
}