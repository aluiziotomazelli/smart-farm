// components/ota_manager/src/ota_manager.cpp
#include "ota_manager.hpp"
#include "manifest_parser.hpp"
#include "version_helper.hpp"
#include "esp_log.h"
#include <iomanip>
#include <sstream>

static const char* TAG = "OtaManager";

namespace {
static constexpr uint32_t OTA_START_BIT = 0x01;
static constexpr uint32_t OTA_STOP_BIT = 0x02;

bool is_version_newer(const OtaVersion& current, const OtaVersion& manifest, bool allow_same)
{
    if (manifest.major > current.major)
        return true;
    if (manifest.major < current.major)
        return false;
    if (manifest.minor > current.minor)
        return true;
    if (manifest.minor < current.minor)
        return false;
    if (manifest.patch > current.patch)
        return true;
    if (manifest.patch < current.patch)
        return false;
    return allow_same;
}

std::string bytes_to_hex(const uint8_t* bytes, size_t len)
{
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) {
        ss << std::setw(2) << static_cast<int>(bytes[i]);
    }
    return ss.str();
}
} // namespace

OtaManager::OtaManager(const OtaDependencies& deps, const OtaConfig& config)
    : deps_(deps)
    , config_(config)
    , status_(OtaStatus::IDLE)
{
}

OtaManager::~OtaManager()
{
    deinit();
}

bool OtaManager::init(const OtaConfig& config)
{
    config_ = config;

    if (state_mutex_ == nullptr) {
        state_mutex_ = deps_.task_scheduler.mutex_create();
        if (state_mutex_ == nullptr) {
            ESP_LOGE(TAG, "Failed to create state mutex");
            return false;
        }
    }

    if (shutdown_done_ == nullptr) {
        shutdown_done_ = deps_.task_scheduler.semaphore_binary_create();
        if (shutdown_done_ == nullptr) {
            ESP_LOGE(TAG, "Failed to create shutdown semaphore");
            deps_.task_scheduler.semaphore_delete(state_mutex_);
            state_mutex_ = nullptr;
            return false;
        }
    }

    set_cancel_requested(false);
    set_status(OtaStatus::IDLE);
    return true;
}

void OtaManager::deinit()
{
    set_cancel_requested(true);
    deps_.ota_session.abort();

    TaskHandle_t worker_handle = nullptr;
    if (state_mutex_ != nullptr && deps_.task_scheduler.semaphore_take(state_mutex_, portMAX_DELAY) == pdPASS) {
        worker_handle = ota_task_handle_;
        deps_.task_scheduler.semaphore_give(state_mutex_);
    }

    if (worker_handle != nullptr) {
        deps_.task_scheduler.notify_task(worker_handle, OTA_STOP_BIT, eSetBits);

        if (shutdown_done_ != nullptr) {
            if (deps_.task_scheduler.semaphore_take(shutdown_done_, pdMS_TO_TICKS(1000)) != pdPASS) {
                ESP_LOGW(TAG, "OTA worker did not stop in time");
            }
        }
    }

    if (state_mutex_ != nullptr && deps_.task_scheduler.semaphore_take(state_mutex_, portMAX_DELAY) == pdPASS) {
        if (ota_task_handle_ != nullptr) {
            deps_.task_scheduler.delete_task(ota_task_handle_);
            ota_task_handle_ = nullptr;
        }
        deps_.task_scheduler.semaphore_give(state_mutex_);
    }

    if (shutdown_done_ != nullptr) {
        deps_.task_scheduler.semaphore_delete(shutdown_done_);
        shutdown_done_ = nullptr;
    }

    if (state_mutex_ != nullptr) {
        deps_.task_scheduler.semaphore_delete(state_mutex_);
        state_mutex_ = nullptr;
    }

    cancel_requested_ = false;
    status_ = OtaStatus::IDLE;
}

bool OtaManager::start_ota()
{
    if (should_cancel()) {
        return false;
    }

    if (get_status() != OtaStatus::IDLE && get_status() != OtaStatus::FAILED) {
        return false;
    }

    set_cancel_requested(false);

    TaskHandle_t worker_handle = nullptr;
    if (state_mutex_ != nullptr && deps_.task_scheduler.semaphore_take(state_mutex_, portMAX_DELAY) == pdPASS) {
        worker_handle = ota_task_handle_;
        deps_.task_scheduler.semaphore_give(state_mutex_);
    }

    if (worker_handle == nullptr) {
        BaseType_t ret = deps_.task_scheduler.create_task(
            ota_task_func, "ota_worker", config_.task_stack_size, this, config_.task_priority, &ota_task_handle_);

        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create OTA task");
            return false;
        }
    }

    deps_.task_scheduler.notify_task(ota_task_handle_, OTA_START_BIT, eSetBits);
    return true;
}

void OtaManager::cancel_ota()
{
    set_cancel_requested(true);
    deps_.ota_session.abort();

    TaskHandle_t worker_handle = nullptr;
    if (state_mutex_ != nullptr && deps_.task_scheduler.semaphore_take(state_mutex_, portMAX_DELAY) == pdPASS) {
        worker_handle = ota_task_handle_;
        deps_.task_scheduler.semaphore_give(state_mutex_);
    }

    if (worker_handle != nullptr) {
        deps_.task_scheduler.notify_task(worker_handle, OTA_STOP_BIT, eSetBits);
    }
}

OtaStatus OtaManager::get_status() const
{
    if (state_mutex_ == nullptr) {
        return status_;
    }

    if (deps_.task_scheduler.semaphore_take(state_mutex_, portMAX_DELAY) != pdPASS) {
        return status_;
    }

    OtaStatus current_status = status_;
    deps_.task_scheduler.semaphore_give(state_mutex_);
    return current_status;
}

bool OtaManager::check_pending_verify() const
{
    return deps_.rollback_manager.is_pending_verify();
}

bool OtaManager::confirm_app_valid()
{
    return deps_.rollback_manager.mark_app_valid() == ESP_OK;
}

void OtaManager::rollback_and_reboot()
{
    deps_.rollback_manager.rollback_and_reboot();
}

void OtaManager::ota_task_func(void* pvParameters)
{
    OtaManager* manager = static_cast<OtaManager*>(pvParameters);
    uint32_t notified_value;

    while (true) {
        if (manager->deps_.task_scheduler.task_notify_wait(0, ULONG_MAX, &notified_value, portMAX_DELAY) == pdPASS) {
            if ((notified_value & OTA_STOP_BIT) == OTA_STOP_BIT) {
                break;
            }
            if ((notified_value & OTA_START_BIT) == OTA_START_BIT) {
                manager->ota_task();
            }
        }
    }

    if (manager->state_mutex_ != nullptr &&
        manager->deps_.task_scheduler.semaphore_take(manager->state_mutex_, portMAX_DELAY) == pdPASS) {
        manager->ota_task_handle_ = nullptr;
        manager->deps_.task_scheduler.semaphore_give(manager->state_mutex_);
    }

    manager->signal_shutdown_done();
    vTaskDelete(NULL);
}

void OtaManager::ota_task()
{
    ESP_LOGI(TAG, "Starting OTA process...");

    if (should_cancel()) {
        set_status(OtaStatus::IDLE);
        deps_.ota_session.abort();
        return;
    }

    // 1. Fetch Manifest
    set_status(OtaStatus::MANIFEST_FETCH);
    std::string manifest_content;
    if (deps_.http_client.fetch(config_.manifest_url, manifest_content) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to fetch manifest");
        if (!should_cancel()) {
            set_status(OtaStatus::FAILED);
        }
        return;
    }

    if (should_cancel()) {
        deps_.ota_session.abort();
        set_status(OtaStatus::IDLE);
        set_cancel_requested(false);
        return;
    }

    // 2. Parse Manifest
    auto manifest_opt = deps_.manifest_parser.parse(manifest_content);
    if (!manifest_opt.has_value()) {
        ESP_LOGE(TAG, "Failed to parse manifest");
        set_status(OtaStatus::FAILED);
        return;
    }
    OtaManifest manifest = manifest_opt.value();

    // 3. Validate Device Type
    if (manifest.device_type != config_.device_type) {
        ESP_LOGE(
            TAG,
            "Device type mismatch: manifest=%s, config=%s",
            manifest.device_type.c_str(),
            config_.device_type.c_str());
        set_status(OtaStatus::FAILED);
        return;
    }

    // 4. Version Check
    set_status(OtaStatus::VERSION_CHECK);
    const esp_app_desc_t* running_app = deps_.system.get_running_app_desc();
    auto current_v_opt = VersionHelper::parse(running_app->version);
    if (!current_v_opt.has_value()) {
        ESP_LOGE(TAG, "Failed to parse current version: %s", running_app->version);
        set_status(OtaStatus::FAILED);
        return;
    }

    if (!is_version_newer(current_v_opt.value(), manifest.version, config_.allow_same_version)) {
        ESP_LOGW(
            TAG,
            "Version is not newer. Current: %s, Manifest: %u.%u.%u",
            running_app->version,
            manifest.version.major,
            manifest.version.minor,
            manifest.version.patch);
        set_status(OtaStatus::FAILED);
        return;
    }

    // 5. Downloading
    set_status(OtaStatus::DOWNLOADING);
    esp_http_client_config_t http_config = {};
    http_config.url = manifest.firmware_url.c_str();
    http_config.timeout_ms = config_.http_timeout_ms;

    if (deps_.ota_session.begin(&http_config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to begin OTA session");
        set_status(OtaStatus::FAILED);
        return;
    }

    if (should_cancel()) {
        deps_.ota_session.abort();
        set_status(OtaStatus::IDLE);
        set_cancel_requested(false);
        return;
    }

    // Double check version from image descriptor
    esp_app_desc_t new_app_info;
    if (deps_.ota_session.get_img_desc(&new_app_info) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get image descriptor");
        deps_.ota_session.abort();
        set_status(OtaStatus::FAILED);
        return;
    }

    auto new_v_opt = VersionHelper::parse(new_app_info.version);
    if (!new_v_opt.has_value() ||
        !is_version_newer(current_v_opt.value(), new_v_opt.value(), config_.allow_same_version)) {
        ESP_LOGE(TAG, "Image version check failed. Current: %s, Image: %s", running_app->version, new_app_info.version);
        deps_.ota_session.abort();
        set_status(OtaStatus::FAILED);
        return;
    }

    esp_err_t ota_ret = ESP_OK;
    while ((ota_ret = deps_.ota_session.perform()) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        if (should_cancel()) {
            deps_.ota_session.abort();
            set_status(OtaStatus::IDLE);
            set_cancel_requested(false);
            return;
        }
    }

    if (ota_ret != ESP_OK || !deps_.ota_session.is_complete()) {
        ESP_LOGE(TAG, "OTA download failed: %s", esp_err_to_name(ota_ret));
        deps_.ota_session.abort();
        set_status(OtaStatus::FAILED);
        return;
    }

    if (should_cancel()) {
        deps_.ota_session.abort();
        set_status(OtaStatus::IDLE);
        set_cancel_requested(false);
        return;
    }

    if (deps_.ota_session.finish() != ESP_OK) {
        ESP_LOGE(TAG, "OTA finish failed");
        set_status(OtaStatus::FAILED);
        return;
    }

    // 6. Verifying Hash
    set_status(OtaStatus::VERIFYING);
    uint8_t sha256[32];
    const esp_partition_t* update_partition = deps_.system.get_update_partition();
    if (deps_.system.get_partition_sha256(update_partition, sha256) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get partition SHA256");
        set_status(OtaStatus::FAILED);
        return;
    }

    std::string calculated_hash = bytes_to_hex(sha256, 32);
    if (calculated_hash != manifest.sha256_hex) {
        ESP_LOGE(TAG, "Hash mismatch! Calc: %s, Manifest: %s", calculated_hash.c_str(), manifest.sha256_hex.c_str());
        set_status(OtaStatus::FAILED);
        return;
    }

    ESP_LOGI(TAG, "OTA Successful!");
    set_status(OtaStatus::READY_TO_RESTART);

    if (config_.restart_on_success) {
        ESP_LOGI(TAG, "Restarting...");
        deps_.system.restart();
    }
}

bool OtaManager::should_cancel() const
{
    if (state_mutex_ == nullptr) {
        return cancel_requested_;
    }

    if (deps_.task_scheduler.semaphore_take(state_mutex_, portMAX_DELAY) != pdPASS) {
        return cancel_requested_;
    }

    bool cancel_requested = cancel_requested_;
    deps_.task_scheduler.semaphore_give(state_mutex_);
    return cancel_requested;
}

void OtaManager::set_status(OtaStatus status)
{
    if (state_mutex_ == nullptr) {
        status_ = status;
        return;
    }

    if (deps_.task_scheduler.semaphore_take(state_mutex_, portMAX_DELAY) == pdPASS) {
        status_ = status;
        deps_.task_scheduler.semaphore_give(state_mutex_);
    }
}

void OtaManager::set_cancel_requested(bool value)
{
    if (state_mutex_ == nullptr) {
        cancel_requested_ = value;
        return;
    }

    if (deps_.task_scheduler.semaphore_take(state_mutex_, portMAX_DELAY) == pdPASS) {
        cancel_requested_ = value;
        deps_.task_scheduler.semaphore_give(state_mutex_);
    }
}

void OtaManager::signal_shutdown_done()
{
    if (shutdown_done_ != nullptr) {
        deps_.task_scheduler.semaphore_give(shutdown_done_);
    }
}
