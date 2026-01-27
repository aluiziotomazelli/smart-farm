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
    : wifi_event_instance_(nullptr)
    , ip_event_instance_(nullptr)
    , sta_netif_ptr_(nullptr)
    , task_handle_(nullptr)
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

esp_err_t WiFiManager::init_nvs()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition invalid, erasing");
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            return err;
        }
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t WiFiManager::init()
{
    if (getState() != State::UNINITIALIZED) {
        ESP_LOGI(TAG, "Already initialized.");
        return ESP_OK;
    }

    esp_err_t err = init_nvs();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Initializing network stack...");
    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to esp_netif_init: %s", esp_err_to_name(err));
        return err;
    }
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Netif already initialized.");
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(err));
        return err;
    }
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Event loop already created.");
    }

    sta_netif_ptr_ = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif_ptr_ == nullptr) {
        sta_netif_ptr_ = esp_netif_create_default_wifi_sta();
    }
    if (sta_netif_ptr_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create default STA netif");
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err                    = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to esp_wifi_init: %s", esp_err_to_name(err));
        esp_netif_destroy(sta_netif_ptr_);
        sta_netif_ptr_ = nullptr;
        return err;
    }
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "WiFi stack already initialized.");
    }

    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              &WiFiManager::wifiEventHandler, this,
                                              &wifi_event_instance_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WIFI handler: %s", esp_err_to_name(err));
        esp_wifi_deinit();
        esp_netif_destroy(sta_netif_ptr_);
        sta_netif_ptr_ = nullptr;
        return err;
    }

    err = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                              &WiFiManager::ipEventHandler, this,
                                              &ip_event_instance_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP handler: %s", esp_err_to_name(err));
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              wifi_event_instance_);
        wifi_event_instance_ = nullptr;
        esp_wifi_deinit();
        esp_netif_destroy(sta_netif_ptr_);
        sta_netif_ptr_ = nullptr;
        return err;
    }

    command_queue_    = xQueueCreate(10, sizeof(Command));
    wifi_event_group_ = xEventGroupCreate();
    if (!command_queue_ || !wifi_event_group_) {
        ESP_LOGE(TAG, "Failed to create queue or event group");
        if (command_queue_) {
            vQueueDelete(command_queue_);
            command_queue_ = nullptr;
        }
        if (wifi_event_group_) {
            vEventGroupDelete(wifi_event_group_);
            wifi_event_group_ = nullptr;
        }
        esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID,
                                              ip_event_instance_);
        ip_event_instance_ = nullptr;
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              wifi_event_instance_);
        wifi_event_instance_ = nullptr;
        esp_wifi_deinit();
        esp_netif_destroy(sta_netif_ptr_);
        sta_netif_ptr_ = nullptr;
        return ESP_ERR_NO_MEM;
    }

    BaseType_t task_created =
        xTaskCreate(wifiTask, "wifi_task", 4096, this, 5, &task_handle_);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create wifi task");
        vQueueDelete(command_queue_);
        command_queue_ = nullptr;
        vEventGroupDelete(wifi_event_group_);
        wifi_event_group_ = nullptr;
        esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID,
                                              ip_event_instance_);
        ip_event_instance_ = nullptr;
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              wifi_event_instance_);
        wifi_event_instance_ = nullptr;
        esp_wifi_deinit();
        esp_netif_destroy(sta_netif_ptr_);
        sta_netif_ptr_ = nullptr;
        return ESP_ERR_NO_MEM;
    }

    xSemaphoreTake(state_mutex_, portMAX_DELAY);
    current_state_ = State::INITIALIZED;
    xSemaphoreGive(state_mutex_);
    ESP_LOGI(TAG, "WiFi Manager initialized.");
    return ESP_OK;
}

esp_err_t WiFiManager::deinit()
{
    State current_state = getState();
    ESP_LOGI(TAG, "Deinitializing WiFi Manager...");
    if (current_state == State::UNINITIALIZED) {
        ESP_LOGI(TAG, "Already uninitialized.");
        return ESP_OK;
    }

    if (current_state >= State::STARTING) {
        ESP_LOGI(TAG, "WiFi is running, stopping first...");

        esp_err_t ret = stop(2000);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "WiFi stopped during deinit");
        }
        else {
            ESP_LOGW(TAG, "WiFi failed to stop during deinit: %s", esp_err_to_name(ret));
        }
    }

    if (wifi_event_instance_ != nullptr) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              wifi_event_instance_);
        wifi_event_instance_ = nullptr;
    }
    if (ip_event_instance_ != nullptr) {
        esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID,
                                              ip_event_instance_);
        ip_event_instance_ = nullptr;
    }

    if (task_handle_ != nullptr) {
        ESP_LOGI(TAG, "Stopping WiFi task...");
        Command cmd = {.id = CommandId::EXIT};
        if (xQueueSend(command_queue_, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
            int retry = 0;
            while (task_handle_ != nullptr && retry < 100) {
                vTaskDelay(pdMS_TO_TICKS(10));
                retry++;
            }
            if (task_handle_ == nullptr) {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        }

        if (task_handle_ != nullptr) {
            ESP_LOGW(TAG, "WiFi task did not exit gracefully, deleting...");
            vTaskDelete(task_handle_);
            task_handle_ = nullptr;
        }
        ESP_LOGI(TAG, "WiFi task terminated.");
    }

    esp_err_t ret = esp_wifi_deinit();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi stack deinitialized.");
    }
    else if (ret == ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGI(TAG, "WiFi stack was already deinitialized.");
    }
    else {
        ESP_LOGW(TAG, "WiFi stack deinit failed: %s", esp_err_to_name(ret));
    }

    if (sta_netif_ptr_ != nullptr) {
        esp_netif_destroy(sta_netif_ptr_);
        sta_netif_ptr_ = nullptr;
    }

    if (command_queue_ != nullptr) {
        vQueueDelete(command_queue_);
        command_queue_ = nullptr;
    }
    if (wifi_event_group_ != nullptr) {
        vEventGroupDelete(wifi_event_group_);
        wifi_event_group_ = nullptr;
    }

    xSemaphoreTake(state_mutex_, portMAX_DELAY);
    current_state_ = State::UNINITIALIZED;
    xSemaphoreGive(state_mutex_);

    ESP_LOGI(TAG, "WiFi Manager deinitialized.");
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
        xEventGroupWaitBits(wifi_event_group_, CONNECTED_BIT | CONNECT_FAILED_BIT,
                            pdTRUE, pdFALSE, pdMS_TO_TICKS(timeout_ms));

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

esp_err_t WiFiManager::connect_async(const std::string &ssid,
                                     const std::string &password)
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

    EventBits_t bits =
        xEventGroupWaitBits(wifi_event_group_, DISCONNECTED_BIT, pdTRUE, pdFALSE,
                            pdMS_TO_TICKS(timeout_ms));

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

void WiFiManager::ipEventHandler(void *arg,
                                 esp_event_base_t base,
                                 int32_t id,
                                 void *data)
{
    WiFiManager *self = static_cast<WiFiManager *>(arg);
    Command cmd       = {.id = CommandId::HANDLE_EVENT_IP, .event_id = id};
    xQueueSendFromISR(self->command_queue_, &cmd, nullptr);
}

void WiFiManager::wifiTask(void *pvParameters)
{
    WiFiManager *self = static_cast<WiFiManager *>(pvParameters);
    Command cmd;
    esp_err_t err;

    for (;;) {
        if (xQueueReceive(self->command_queue_, &cmd, portMAX_DELAY) == pdTRUE) {
            // State is locked only during modification
            xSemaphoreTake(self->state_mutex_, portMAX_DELAY);

            switch (cmd.id) {
            case CommandId::START:
                self->current_state_ = State::STARTING;
                if ((err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to set wifi mode: %s", esp_err_to_name(err));
                }
                if ((err = esp_wifi_start()) != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to start wifi: %s", esp_err_to_name(err));
                }
                break;
            case CommandId::STOP:
                self->current_state_ = State::STOPPING;
                if ((err = esp_wifi_stop()) != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to stop wifi: %s", esp_err_to_name(err));
                }
                break;
            case CommandId::CONNECT:
                self->current_state_ = State::CONNECTING;
                {
                    wifi_config_t wifi_config = {};
                    strncpy((char *)wifi_config.sta.ssid, cmd.ssid.c_str(),
                            sizeof(wifi_config.sta.ssid) - 1);
                    strncpy((char *)wifi_config.sta.password, cmd.password.c_str(),
                            sizeof(wifi_config.sta.password) - 1);
                    if ((err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config)) != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to set wifi config: %s",
                                 esp_err_to_name(err));
                    }
                    if ((err = esp_wifi_connect()) != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to connect wifi: %s", esp_err_to_name(err));
                    }
                }
                break;
            case CommandId::DISCONNECT:
                self->current_state_ = State::DISCONNECTING;
                if ((err = esp_wifi_disconnect()) != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to disconnect wifi: %s", esp_err_to_name(err));
                }
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

            case CommandId::EXIT:
                ESP_LOGI(TAG, "WiFi Task exiting...");
                xSemaphoreGive(self->state_mutex_);
                self->task_handle_ = nullptr;
                vTaskDelete(NULL);
                return; // Should not reach here
            }
            xSemaphoreGive(self->state_mutex_);
        }
    }
}