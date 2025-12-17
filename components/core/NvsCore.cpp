#include "NvsCore.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "NvsCore";

static constexpr const char *NVS_NAMESPACE = "core";
static constexpr const char *NVS_KEY       = "state";

CoreStorage NvsCore::core_ = {};

/* =========================
 *  Init
 * ========================= */
esp_err_t NvsCore::init()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "NVS partition invalid, erasing");
        nvs_flash_erase();
        err = nvs_flash_init();
    }

    if (err != ESP_OK)
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));

    return err;
}

/* =========================
 *  Load
 * ========================= */
esp_err_t NvsCore::load()
{
    nvs_handle_t handle;
    esp_err_t    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "No NVS namespace, applying defaults");
        apply_defaults();
        commit();
        return ESP_OK;
    }

    size_t size = sizeof(CoreStorage);
    err         = nvs_get_blob(handle, NVS_KEY, &core_, &size);
    nvs_close(handle);

    if (err != ESP_OK || size != sizeof(CoreStorage))
    {
        ESP_LOGW(TAG, "Invalid or missing core blob, resetting");
        apply_defaults();
        commit();
        return ESP_OK;
    }

    if (core_.schema_version != CORE_SCHEMA_VERSION)
    {
        ESP_LOGW(TAG, "Schema mismatch: %lu -> %lu", core_.schema_version, CORE_SCHEMA_VERSION);

        err = migrate(core_.schema_version);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Migration failed, factory reset");
            factory_reset();
        }
        else
        {
            commit();
        }
    }

    return ESP_OK;
}

/* =========================
 *  Commit
 * ========================= */
esp_err_t NvsCore::commit()
{
    nvs_handle_t handle;
    esp_err_t    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;

    err = nvs_set_blob(handle, NVS_KEY, &core_, sizeof(CoreStorage));
    if (err == ESP_OK)
        err = nvs_commit(handle);

    nvs_close(handle);
    return err;
}

/* =========================
 *  Access
 * ========================= */
CoreStorage &NvsCore::data() { return core_; }

/* =========================
 *  Factory reset
 * ========================= */
void NvsCore::factory_reset()
{
    ESP_LOGW(TAG, "Factory reset core");
    memset(&core_, 0, sizeof(CoreStorage));
    apply_defaults();
    commit();
}

/* =========================
 *  Defaults
 * ========================= */
void NvsCore::apply_defaults()
{
    memset(&core_, 0, sizeof(CoreStorage));

    core_.schema_version = CORE_SCHEMA_VERSION;

    core_.identity.node_id     = 0;
    core_.identity.type        = NodeType::UNKNOWN;
    core_.identity.hw_revision = 0;

    core_.fw_version = {0, 0, 0};

    core_.ota.state        = OtaState::IDLE;
    core_.ota.fail_count   = 0;
    core_.ota.last_boot_ok = true;

    core_.lifecycle.mode        = LifecycleMode::NORMAL;
    core_.lifecycle.boot_count  = 0;
    core_.lifecycle.crash_count = 0;

    core_.time.has_valid_time   = false;
    core_.time.unix_time        = 0;
    core_.time.last_sync_uptime = 0;

    core_.power.profile          = PowerProfile::ALWAYS_ON;
    core_.power.sleep_interval_s = 0;

    core_.wake.last_wake      = WakeSource::NONE;
    core_.wake.next_wake_hint = WakeHint::NONE;
}

/* =========================
 *  Migration
 * ========================= */
esp_err_t NvsCore::migrate(uint32_t from_version)
{
    switch (from_version)
    {
    default:
        return ESP_ERR_NOT_SUPPORTED;
    }
}
