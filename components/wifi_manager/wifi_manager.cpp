#include "wifi_manager.hpp"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
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
    , is_credential_valid_(false)
    , retry_count_(0)
    , next_reconnect_ms_(0)
{
    // Mutex is created once and persists for the lifetime of the singleton.
    // We use a recursive mutex because the wifiTask holds it while calling
    // internal methods (like saveValidFlag) that also need it.
    state_mutex_ = xSemaphoreCreateRecursiveMutex();
}

WiFiManager::~WiFiManager()
{
    // Cleanup if the object is ever destroyed (singleton usually lasts until
    // shutdown)
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
    // NVS is required for the WiFi driver to store internal
    // configurations/calibration
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
    xSemaphoreTakeRecursive(state_mutex_, portMAX_DELAY);
    if (current_state_ != State::UNINITIALIZED) {
        xSemaphoreGiveRecursive(state_mutex_);
        ESP_LOGI(TAG, "Already initialized or initializing.");
        return ESP_OK;
    }
    // Set to INITIALIZING to prevent concurrent init calls
    current_state_ = State::INITIALIZING;
    xSemaphoreGiveRecursive(state_mutex_);

    // Global NVS init - not rolled back by deinit() as it's shared across the system
    esp_err_t err = init_nvs();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(err));
        return err;
    }

    // Global Netif init - allowed to fail if already initialized by another
    // component
    err = esp_netif_init();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Netif initialized.");
    }
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to esp_netif_init: %s", esp_err_to_name(err));
        return err;
    }
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Netif already initialized.");
    }

    // Default event loop is shared - we don't delete it in deinit() to avoid
    // breaking others
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(err));
        deinit(); // Component-specific setup failed, clean up allocated members
        return err;
    }
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Event loop already created.");
    }

    // Check if the WiFi station interface already exists (idempotency)
    sta_netif_ptr_ = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif_ptr_ != nullptr) {
        ESP_LOGW(TAG, "Using existing default STA netif");
    }
    if (sta_netif_ptr_ == nullptr) {
        sta_netif_ptr_ = esp_netif_create_default_wifi_sta();
    }
    if (sta_netif_ptr_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create default STA netif");
        deinit();
        return ESP_FAIL;
    }

    // Initialize the WiFi driver stack
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err                    = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to esp_wifi_init: %s", esp_err_to_name(err));
        deinit();
        return err;
    }
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "WiFi stack already initialized.");
    }

    // Register event handlers with instance pointers for the static callbacks
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              &WiFiManager::wifiEventHandler, this,
                                              &wifi_event_instance_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WIFI handler: %s", esp_err_to_name(err));
        deinit();
        return err;
    }

    err = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                              &WiFiManager::ipEventHandler, this,
                                              &ip_event_instance_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP handler: %s", esp_err_to_name(err));
        deinit();
        return err;
    }

    // RTOS resources for communication between the API and the internal task
    command_queue_    = xQueueCreate(10, sizeof(Command));
    wifi_event_group_ = xEventGroupCreate();
    if (!command_queue_ || !wifi_event_group_) {
        ESP_LOGE(TAG, "Failed to create queue or event group");
        deinit();
        return ESP_ERR_NO_MEM;
    }

    // Load validity flag from NVS
    nvs_handle_t h;
    err = nvs_open("wifi_manager", NVS_READONLY, &h);
    if (err == ESP_OK) {
        uint8_t valid = 0;
        if (nvs_get_u8(h, "valid", &valid) == ESP_OK) {
            is_credential_valid_ = (valid != 0);
        }
        nvs_close(h);
    }
    else {
        // If it doesn't exist, we'll determine it after checking driver config
        is_credential_valid_ = false;
    }

    // Check if the driver already has configuration
    wifi_config_t current_conf;
    if (esp_wifi_get_config(WIFI_IF_STA, &current_conf) == ESP_OK) {
        if (strlen((char *)current_conf.sta.ssid) == 0) {
            // No SSID in driver, check Kconfig
            if (strlen(CONFIG_WIFI_SSID) > 0) {
                ESP_LOGI(TAG, "No SSID in driver, using Kconfig default: %s",
                         CONFIG_WIFI_SSID);
                wifi_config_t wifi_config = {};
                strncpy((char *)wifi_config.sta.ssid, CONFIG_WIFI_SSID,
                        sizeof(wifi_config.sta.ssid) - 1);
                strncpy((char *)wifi_config.sta.password, CONFIG_WIFI_PASSWORD,
                        sizeof(wifi_config.sta.password) - 1);
                esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
                is_credential_valid_ = true;
                saveValidFlag(true);
            }
        }
        else {
            ESP_LOGI(TAG, "Driver already has SSID: %s", current_conf.sta.ssid);
            // If we didn't find the flag in NVS but driver has SSID, assume valid
            // unless we specifically failed before
            err = nvs_open("wifi_manager", NVS_READONLY, &h);
            if (err != ESP_OK) {
                is_credential_valid_ = true;
                saveValidFlag(true);
            }
        }
    }

    // Launch the consumer task that executes all driver operations
    BaseType_t task_created =
        xTaskCreate(wifiTask, "wifi_task", 4096, this, 5, &task_handle_);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create wifi task");
        deinit();
        return ESP_ERR_NO_MEM;
    }

    xSemaphoreTakeRecursive(state_mutex_, portMAX_DELAY);
    current_state_ = State::INITIALIZED;
    xSemaphoreGiveRecursive(state_mutex_);
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

    // 1. Ensure WiFi is stopped before deinitializing the stack
    if (current_state >= State::STARTING && current_state < State::STOPPING) {
        ESP_LOGI(TAG, "WiFi is running, stopping first...");
        stop(2000);
    }

    // 2. Terminate the manager task gracefully using the EXIT command
    if (task_handle_ != nullptr) {
        ESP_LOGI(TAG, "Stopping WiFi task...");
        Command cmd = {};
        cmd.id      = CommandId::EXIT;
        if (command_queue_ != nullptr &&
            xQueueSend(command_queue_, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Wait for the task to self-delete and nullify its handle
            int retry = 0;
            while (task_handle_ != nullptr && retry < 100) {
                vTaskDelay(pdMS_TO_TICKS(10));
                retry++;
            }
            // Small safety delay to ensure FreeRTOS has finished task cleanup
            if (task_handle_ == nullptr) {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        }

        // Forced deletion if graceful exit fails
        if (task_handle_ != nullptr) {
            ESP_LOGW(TAG, "WiFi task did not exit gracefully, deleting...");
            vTaskDelete(task_handle_);
            task_handle_ = nullptr;
        }
        ESP_LOGI(TAG, "WiFi task terminated.");
    }

    // 3. Deinit the driver stack
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

    // 4. Specifically destroy the default wifi sta netif to allow reuse after
    // re-init
    if (sta_netif_ptr_ != nullptr) {
        esp_netif_destroy_default_wifi(sta_netif_ptr_);
        sta_netif_ptr_ = nullptr;
    }

    // 5. Unregister event handlers to prevent calling deleted instance members
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

    // 6. Delete the default event loop (DISABLED: usually too global to delete)
    // esp_event_loop_delete_default();

    // 7. Clean up internal RTOS synchronization objects
    if (command_queue_ != nullptr) {
        vQueueDelete(command_queue_);
        command_queue_ = nullptr;
    }
    if (wifi_event_group_ != nullptr) {
        vEventGroupDelete(wifi_event_group_);
        wifi_event_group_ = nullptr;
    }

    xSemaphoreTakeRecursive(state_mutex_, portMAX_DELAY);
    current_state_ = State::UNINITIALIZED;
    xSemaphoreGiveRecursive(state_mutex_);

    ESP_LOGI(TAG, "WiFi Manager deinitialized.");
    return ESP_OK;
}

esp_err_t WiFiManager::start(uint32_t timeout_ms)
{
    if (getState() == State::UNINITIALIZED || !wifi_event_group_ ||
        !command_queue_) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "API: Requesting to start WiFi...");
    Command cmd = {};
    cmd.id      = CommandId::START;

    xEventGroupClearBits(wifi_event_group_, STARTED_BIT | START_FAILED_BIT);
    esp_err_t err = sendCommand(cmd, false);
    if (err != ESP_OK) {
        return err;
    }

    // Wait for the Task to set the success or failure bit
    EventBits_t bits =
        xEventGroupWaitBits(wifi_event_group_, STARTED_BIT | START_FAILED_BIT,
                            pdTRUE, pdFALSE, pdMS_TO_TICKS(timeout_ms));

    if (bits & STARTED_BIT) {
        return ESP_OK;
    }
    if (bits & START_FAILED_BIT) {
        return ESP_FAIL;
    }

    // Rollback: if we timed out waiting for the driver, try to stop it to reset
    // state
    ESP_LOGW(TAG, "Start timed out, cancelling...");
    stop_async();
    return ESP_ERR_TIMEOUT;
}

esp_err_t WiFiManager::start_async()
{
    if (getState() == State::UNINITIALIZED || !command_queue_) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "API: Requesting to start WiFi (async)...");
    Command cmd = {};
    cmd.id      = CommandId::START;
    return sendCommand(cmd, true);
}

esp_err_t WiFiManager::stop(uint32_t timeout_ms)
{
    if (getState() == State::UNINITIALIZED || !wifi_event_group_ ||
        !command_queue_) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "API: Requesting to stop WiFi...");
    Command cmd = {};
    cmd.id      = CommandId::STOP;

    xEventGroupClearBits(wifi_event_group_, STOPPED_BIT | STOP_FAILED_BIT);
    esp_err_t err = sendCommand(cmd, false);
    if (err != ESP_OK) {
        return err;
    }

    EventBits_t bits =
        xEventGroupWaitBits(wifi_event_group_, STOPPED_BIT | STOP_FAILED_BIT, pdTRUE,
                            pdFALSE, pdMS_TO_TICKS(timeout_ms));

    if (bits & STOPPED_BIT) {
        return ESP_OK;
    }
    if (bits & STOP_FAILED_BIT) {
        return ESP_FAIL;
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t WiFiManager::stop_async()
{
    if (getState() == State::UNINITIALIZED || !command_queue_) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "API: Requesting to stop WiFi (async)...");
    Command cmd = {};
    cmd.id      = CommandId::STOP;
    return sendCommand(cmd, true);
}

esp_err_t WiFiManager::connect(uint32_t timeout_ms)
{
    if (getState() == State::UNINITIALIZED || !wifi_event_group_ ||
        !command_queue_) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "API: Requesting to connect (sync)...");
    Command cmd = {};
    cmd.id      = CommandId::CONNECT;

    xEventGroupClearBits(wifi_event_group_, CONNECTED_BIT | CONNECT_FAILED_BIT);
    esp_err_t err = sendCommand(cmd, false);
    if (err != ESP_OK) {
        return err;
    }

    // Wait for either the GOT_IP event (SUCCESS) or a DISCONNECT/ERROR event (FAIL)
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
        // Rollback: if timeout occurs, cancel the driver connection attempt
        ESP_LOGW(TAG, "Connect timed out, cancelling attempt...");
        disconnect_async();
        return ESP_ERR_TIMEOUT;
    }
}

esp_err_t WiFiManager::connect()
{
    if (getState() == State::UNINITIALIZED || !command_queue_) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "API: Requesting to connect (async)...");
    Command cmd = {};
    cmd.id      = CommandId::CONNECT;
    return sendCommand(cmd, true);
}

esp_err_t WiFiManager::connect(const std::string &ssid,
                               const std::string &password,
                               uint32_t timeout_ms)
{
    esp_err_t err = setCredentials(ssid, password);
    if (err != ESP_OK)
        return err;
    return connect(timeout_ms);
}

esp_err_t WiFiManager::connect_async(const std::string &ssid,
                                     const std::string &password)
{
    esp_err_t err = setCredentials(ssid, password);
    if (err != ESP_OK)
        return err;
    return connect();
}

esp_err_t WiFiManager::disconnect(uint32_t timeout_ms)
{
    if (getState() == State::UNINITIALIZED || !wifi_event_group_ ||
        !command_queue_) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "API: Requesting to disconnect (sync)...");
    Command cmd = {};
    cmd.id      = CommandId::DISCONNECT;

    xEventGroupClearBits(wifi_event_group_, DISCONNECTED_BIT | CONNECT_FAILED_BIT);
    esp_err_t err = sendCommand(cmd, false);
    if (err != ESP_OK) {
        return err;
    }

    EventBits_t bits =
        xEventGroupWaitBits(wifi_event_group_, DISCONNECTED_BIT | CONNECT_FAILED_BIT,
                            pdTRUE, pdFALSE, pdMS_TO_TICKS(timeout_ms));

    if (bits & DISCONNECTED_BIT) {
        return ESP_OK;
    }
    if (bits & CONNECT_FAILED_BIT) {
        return ESP_FAIL;
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t WiFiManager::disconnect_async()
{
    if (getState() == State::UNINITIALIZED || !command_queue_) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "API: Requesting to disconnect (async)...");
    Command cmd = {};
    cmd.id      = CommandId::DISCONNECT;
    return sendCommand(cmd, true);
}

WiFiManager::State WiFiManager::getState() const
{
    // The Mutex ensures that we don't read the state while the Task is
    // mid-transition
    xSemaphoreTakeRecursive(state_mutex_, portMAX_DELAY);
    State state = current_state_;
    xSemaphoreGiveRecursive(state_mutex_);
    return state;
}

// =================================================================================================
// Credentials and Reset
// =================================================================================================

esp_err_t WiFiManager::setCredentials(const std::string &ssid,
                                      const std::string &password)
{
    if (getState() == State::UNINITIALIZED || !command_queue_) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "API: Setting credentials...");
    Command cmd    = {};
    cmd.id         = CommandId::SET_CREDENTIALS;
    cmd.ssid       = ssid;
    cmd.password   = password;
    return sendCommand(cmd, false); // Sync to ensure it's applied
}

esp_err_t WiFiManager::getCredentials(std::string &ssid, std::string &password)
{
    wifi_config_t conf;
    esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &conf);
    if (err == ESP_OK) {
        ssid     = (char *)conf.sta.ssid;
        password = (char *)conf.sta.password;
    }
    return err;
}

esp_err_t WiFiManager::clearCredentials()
{
    if (getState() == State::UNINITIALIZED || !command_queue_) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "API: Clearing credentials...");
    Command cmd = {};
    cmd.id      = CommandId::CLEAR_CREDENTIALS;
    return sendCommand(cmd, false);
}

esp_err_t WiFiManager::factoryReset()
{
    if (getState() == State::UNINITIALIZED || !command_queue_) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "API: Factory reset...");
    Command cmd = {};
    cmd.id      = CommandId::FACTORY_RESET;
    return sendCommand(cmd, false);
}

bool WiFiManager::isCredentialsValid() const
{
    xSemaphoreTakeRecursive(state_mutex_, portMAX_DELAY);
    bool valid = is_credential_valid_;
    xSemaphoreGiveRecursive(state_mutex_);
    return valid;
}

esp_err_t WiFiManager::saveValidFlag(bool valid)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("wifi_manager", NVS_READWRITE, &h);
    if (err != ESP_OK)
        return err;

    err = nvs_set_u8(h, "valid", valid ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    xSemaphoreTakeRecursive(state_mutex_, portMAX_DELAY);
    is_credential_valid_ = valid;
    xSemaphoreGiveRecursive(state_mutex_);

    return err;
}

// =================================================================================================
// Internal Implementation
// =================================================================================================

esp_err_t WiFiManager::sendCommand(const Command &cmd, bool is_async)
{
    // Basic safety check: don't even try to queue if we're not initialized
    if (getState() == State::UNINITIALIZED) {
        return ESP_ERR_INVALID_STATE;
    }

    // Synchronous calls wait forever for a slot, async fail immediately if queue
    // full
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
    // Bridge from the system event loop back to our managed task
    WiFiManager *self = static_cast<WiFiManager *>(arg);
    Command cmd       = {};
    cmd.id            = CommandId::HANDLE_EVENT_WIFI;
    cmd.event_id      = id;

    if (id == WIFI_EVENT_STA_DISCONNECTED && data != nullptr) {
        wifi_event_sta_disconnected_t *disconn =
            static_cast<wifi_event_sta_disconnected_t *>(data);
        cmd.reason = disconn->reason;
    }

    xQueueSendFromISR(self->command_queue_, &cmd, nullptr);
}

void WiFiManager::ipEventHandler(void *arg,
                                 esp_event_base_t base,
                                 int32_t id,
                                 void *data)
{
    WiFiManager *self = static_cast<WiFiManager *>(arg);
    Command cmd       = {};
    cmd.id            = CommandId::HANDLE_EVENT_IP;
    cmd.event_id      = id;
    xQueueSendFromISR(self->command_queue_, &cmd, nullptr);
}

void WiFiManager::wifiTask(void *pvParameters)
{
    WiFiManager *self = static_cast<WiFiManager *>(pvParameters);
    Command cmd;
    esp_err_t err;

    for (;;) {
        TickType_t wait_ticks = portMAX_DELAY;

        // If we are waiting to reconnect, calculate the remaining backoff time
        if (self->current_state_ == State::WAITING_RECONNECT) {
            uint64_t now = esp_timer_get_time() / 1000;
            if (self->next_reconnect_ms_ > now) {
                wait_ticks = pdMS_TO_TICKS(self->next_reconnect_ms_ - now);
            }
            else {
                wait_ticks = 0; // Timer already expired
            }
        }

        // Wait for a command or a timeout (backoff expiration)
        if (xQueueReceive(self->command_queue_, &cmd, wait_ticks) == pdTRUE) {
            // The state mutex is held for the duration of a command processing
            xSemaphoreTakeRecursive(self->state_mutex_, portMAX_DELAY);

            // Any explicit user command resets the retry counter
            if (cmd.id != CommandId::HANDLE_EVENT_WIFI &&
                cmd.id != CommandId::HANDLE_EVENT_IP) {
                self->retry_count_ = 0;
            }

            switch (cmd.id) {
            case CommandId::START:
            {
                State s = self->current_state_;
                // Filter redundant start calls
                if (s >= State::STARTED && s <= State::ERROR_CREDENTIALS) {
                    ESP_LOGI(TAG, "Already started (state: %d)", (int)s);
                    xEventGroupSetBits(self->wifi_event_group_, STARTED_BIT);
                    break;
                }
                self->current_state_ = State::STARTING;
                if ((err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to set wifi mode: %s",
                             esp_err_to_name(err));
                    self->current_state_ =
                        s; // Rollback to previous state on immediate failure
                    xEventGroupSetBits(self->wifi_event_group_, START_FAILED_BIT);
                }
                else if ((err = esp_wifi_start()) != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to start wifi: %s", esp_err_to_name(err));
                    self->current_state_ = s;
                    xEventGroupSetBits(self->wifi_event_group_, START_FAILED_BIT);
                }
                break;
            }
            case CommandId::STOP:
            {
                State s = self->current_state_;
                if (s == State::STOPPED || s == State::INITIALIZED) {
                    ESP_LOGI(TAG, "Already stopped (state: %d)", (int)s);
                    xEventGroupSetBits(self->wifi_event_group_, STOPPED_BIT);
                    break;
                }
                self->current_state_ = State::STOPPING;
                if ((err = esp_wifi_stop()) != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to stop wifi: %s", esp_err_to_name(err));
                    self->current_state_ = s;
                    xEventGroupSetBits(self->wifi_event_group_, STOP_FAILED_BIT);
                }
                break;
            }
            case CommandId::CONNECT:
            {
                State s = self->current_state_;

                // If credentials are provided in the command, set them first
                if (!cmd.ssid.empty()) {
                    wifi_config_t wifi_config = {};
                    strncpy((char *)wifi_config.sta.ssid, cmd.ssid.c_str(),
                            sizeof(wifi_config.sta.ssid) - 1);
                    strncpy((char *)wifi_config.sta.password, cmd.password.c_str(),
                            sizeof(wifi_config.sta.password) - 1);
                    if ((err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config)) !=
                        ESP_OK) {
                        ESP_LOGE(TAG, "Failed to set wifi config (in connect): %s",
                                 esp_err_to_name(err));
                    }
                    else {
                        self->saveValidFlag(true);
                    }
                }

                // Basic state validation before attempting a connect
                if (s == State::CONNECTED_GOT_IP) {
                    ESP_LOGI(TAG, "Already connected.");
                    xEventGroupSetBits(self->wifi_event_group_, CONNECTED_BIT);
                    break;
                }
                if (s < State::STARTED || s >= State::STOPPING) {
                    ESP_LOGE(TAG, "WiFi not started. Cannot connect (state: %d)",
                             (int)s);
                    xEventGroupSetBits(self->wifi_event_group_, CONNECT_FAILED_BIT);
                    break;
                }
                if (s == State::CONNECTING || s == State::CONNECTED_NO_IP) {
                    ESP_LOGW(TAG, "Connection already in progress (state: %d)",
                             (int)s);
                    break;
                }

                self->current_state_ = State::CONNECTING;
                if ((err = esp_wifi_connect()) != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to connect wifi: %s", esp_err_to_name(err));
                    self->current_state_ = s;
                    xEventGroupSetBits(self->wifi_event_group_, CONNECT_FAILED_BIT);
                }
                break;
            }
            case CommandId::SET_CREDENTIALS:
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
                else {
                    ESP_LOGI(TAG, "Credentials set successfully.");
                    self->saveValidFlag(true);
                }
                break;
            }
            case CommandId::CLEAR_CREDENTIALS:
            {
                wifi_config_t blank_config = {};
                // Setting a config with empty SSID effectively clears it
                if ((err = esp_wifi_set_config(WIFI_IF_STA, &blank_config)) !=
                    ESP_OK) {
                    ESP_LOGE(TAG, "Failed to clear wifi config: %s",
                             esp_err_to_name(err));
                }
                else {
                    ESP_LOGI(TAG, "Credentials cleared.");
                    self->saveValidFlag(false);
                }
                break;
            }
            case CommandId::FACTORY_RESET:
            {
                esp_wifi_restore();
                nvs_handle_t h;
                if (nvs_open("wifi_manager", NVS_READWRITE, &h) == ESP_OK) {
                    nvs_erase_all(h);
                    nvs_commit(h);
                    nvs_close(h);
                }
                self->is_credential_valid_ = false;
                ESP_LOGI(TAG, "Factory reset complete.");
                break;
            }
            case CommandId::DISCONNECT:
            {
                State s = self->current_state_;
                if (s == State::DISCONNECTED || s == State::STOPPED ||
                    s == State::INITIALIZED) {
                    ESP_LOGI(TAG, "Already disconnected (state: %d)", (int)s);
                    xEventGroupSetBits(self->wifi_event_group_, DISCONNECTED_BIT);
                    break;
                }

                // SPECIAL CASE: Rollback during early connect phase.
                // The driver might not emit a DISCONNECTED event if we call
                // disconnect before the link is established.
                if (s == State::CONNECTING) {
                    ESP_LOGI(TAG, "Disconnect requested while connecting, forcing "
                                  "DISCONNECTED");
                    esp_wifi_disconnect();
                    self->current_state_ = State::DISCONNECTED;
                    xEventGroupSetBits(self->wifi_event_group_, DISCONNECTED_BIT);
                    break;
                }

                self->current_state_ = State::DISCONNECTING;
                if ((err = esp_wifi_disconnect()) != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to disconnect wifi: %s",
                             esp_err_to_name(err));
                    self->current_state_ = s;
                    xEventGroupSetBits(self->wifi_event_group_, CONNECT_FAILED_BIT);
                }
                break;
            }

            case CommandId::HANDLE_EVENT_WIFI:
                // Handle events from the WiFi driver and transition states
                // accordingly
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
                    ESP_LOGI(TAG, "Task Event: STA_DISCONNECTED (reason: %d)",
                             cmd.reason);

                    State s = self->current_state_;
                    // Case 1: Disconnection was intended (via API)
                    if (s == State::DISCONNECTING || s == State::STOPPING ||
                        s == State::INITIALIZED) {
                        self->current_state_ = State::DISCONNECTED;
                        xEventGroupSetBits(self->wifi_event_group_,
                                           DISCONNECTED_BIT | CONNECT_FAILED_BIT);
                    }
                    // Case 2: Permanent failure (Wrong credentials)
                    else if (cmd.reason == WIFI_REASON_AUTH_FAIL) {
                        ESP_LOGE(TAG, "Authentication failed (wrong password).");
                        self->current_state_ = State::ERROR_CREDENTIALS;
                        self->saveValidFlag(false);
                        xEventGroupSetBits(self->wifi_event_group_,
                                           CONNECT_FAILED_BIT);
                    }
                    // Case 3: Temporary failure, initiate reconnection logic
                    else {
                        if (self->is_credential_valid_) {
                            self->current_state_ = State::WAITING_RECONNECT;
                            self->retry_count_++;

                            uint32_t delay_ms = 1000; // First retry after 1s
                            if (self->retry_count_ > 1) {
                                // Exponential backoff: 2s, 4s, 8s... max 300s (5 min)
                                delay_ms = (1 << (self->retry_count_ - 1)) * 1000;
                                if (delay_ms > 300000)
                                    delay_ms = 300000;
                            }
                            self->next_reconnect_ms_ =
                                (esp_timer_get_time() / 1000) + delay_ms;

                            ESP_LOGI(TAG, "Reconnection attempt %lu in %lu ms...",
                                     (unsigned long)self->retry_count_,
                                     (unsigned long)delay_ms);
                        }
                        else {
                            ESP_LOGW(TAG, "Credentials invalid, not reconnecting.");
                            self->current_state_ = State::DISCONNECTED;
                        }

                        // Signal failure so blocking connect() calls can return
                        xEventGroupSetBits(self->wifi_event_group_,
                                           CONNECT_FAILED_BIT);
                    }
                    break;
                }
                break;

            case CommandId::HANDLE_EVENT_IP:
                if (cmd.event_id == IP_EVENT_STA_GOT_IP) {
                    ESP_LOGI(TAG, "Task Event: GOT_IP");
                    self->current_state_ = State::CONNECTED_GOT_IP;
                    self->retry_count_   = 0; // Reset retries on success
                    if (!self->is_credential_valid_) {
                        self->saveValidFlag(true);
                    }
                    xEventGroupSetBits(self->wifi_event_group_, CONNECTED_BIT);
                }
                break;

            case CommandId::EXIT:
                ESP_LOGI(TAG, "WiFi Task exiting...");
                // Mutex must be released before the task disappears
                xSemaphoreGiveRecursive(self->state_mutex_);
                self->task_handle_ = nullptr; // Signal to deinit() that we are gone
                vTaskDelete(NULL);
                return; // Should not reach here
            }
            xSemaphoreGiveRecursive(self->state_mutex_);
        }
        else {
            // Timeout reached in xQueueReceive - Backoff finished
            if (self->current_state_ == State::WAITING_RECONNECT) {
                xSemaphoreTakeRecursive(self->state_mutex_, portMAX_DELAY);
                if (self->is_credential_valid_) {
                    ESP_LOGI(TAG, "Retrying connection...");
                    self->current_state_ = State::CONNECTING;
                    esp_wifi_connect();
                }
                else {
                    self->current_state_ = State::DISCONNECTED;
                }
                xSemaphoreGiveRecursive(self->state_mutex_);
            }
        }
    }
}

#ifdef UNIT_TEST

esp_err_t WiFiManager::testHelper_sendStartCommand(bool is_async)
{
    Command cmd = {};
    cmd.id      = CommandId::START;
    return sendCommand(cmd, is_async);
}

esp_err_t WiFiManager::testHelper_sendStopCommand(bool is_async)
{
    Command cmd = {};
    cmd.id      = CommandId::STOP;
    return sendCommand(cmd, is_async);
}

esp_err_t WiFiManager::testHelper_sendConnectCommand(const std::string &ssid,
                                                     const std::string &password,
                                                     bool is_async)
{
    Command cmd  = {};
    cmd.id       = CommandId::CONNECT;
    cmd.ssid     = ssid;
    cmd.password = password;
    return sendCommand(cmd, is_async);
}

esp_err_t WiFiManager::testHelper_sendDisconnectCommand(bool is_async)
{
    Command cmd = {};
    cmd.id      = CommandId::DISCONNECT;
    return sendCommand(cmd, is_async);
}

uint32_t WiFiManager::testHelper_getQueuePendingCount() const
{
    if (!command_queue_)
        return 0;
    return uxQueueMessagesWaiting(command_queue_);
}

bool WiFiManager::testHelper_isQueueFull() const
{
    if (!command_queue_)
        return true;
    return uxQueueSpacesAvailable(command_queue_) == 0;
}
#endif // UNIT_TEST