#include "nvs_core.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"
#include <cstring>

static const char *TAG = "NvsCore";

NvsCore::NvsCore(const char *ns)
    : _namespace(ns)
{
    // Garante que core inicie zerado
    memset(&core_, 0, sizeof(CoreStorage));
}

esp_err_t NvsCore::init_partition()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition invalid, erasing");
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t NvsCore::open_nvs(nvs_open_mode_t mode)
{
    if (_isOpen)
        return ESP_OK; // Já aberto
    esp_err_t err = nvs_open(_namespace, mode, &_handle);
    if (err == ESP_OK)
        _isOpen = true;
    return err;
}

void NvsCore::close_nvs()
{
    if (_isOpen) {
        nvs_close(_handle);
        _isOpen = false;
    }
}

esp_err_t NvsCore::load()
{
    esp_err_t err = open_nvs(NVS_READONLY);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for reading: %s", esp_err_to_name(err));
        return err;
    }

    // 1. Carrega Core Data
    err = loadStruct("core_data", core_);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Core data not found or corrupt: %s", esp_err_to_name(err));
        close_nvs();
        return err;
    }

    // Validação de Schema do Core
    if (core_.schema_version != CORE_SCHEMA_VERSION) {
        ESP_LOGW(TAG, "Schema mismatch. Migrating...");
        // logica de migração ou reset...
        core_.schema_version = CORE_SCHEMA_VERSION;
    }

    // 2. Chama o filho para carregar seus dados
    err = loadAppData();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "App data not found or corrupt: %s", esp_err_to_name(err));
        // Não reseta aqui, apenas propaga o erro
    }

    close_nvs();
    return err; // Retorna o status final (pode ser erro do app)
}

esp_err_t NvsCore::commit()
{
    esp_err_t err = open_nvs(NVS_READWRITE);
    if (err != ESP_OK)
        return err;

    // 1. Salva Core
    err = saveStruct("core_data", core_);
    if (err != ESP_OK) {
        close_nvs();
        return err;
    }

    // 2. Chama filho para salvar
    err = saveAppData();

    // 3. Commit efetivo na flash
    if (err == ESP_OK) {
        err = nvs_commit(_handle);
    }

    close_nvs();
    return err;
}

void NvsCore::apply_core_defaults()
{
    memset(&core_, 0, sizeof(CoreStorage));
    core_.schema_version = CORE_SCHEMA_VERSION;
    core_.node_type      = NodeType::UNKNOWN; // Será sobrescrito pelo App
    // ... resto dos defaults do core ...
}

void NvsCore::factory_reset()
{
    erase_namespace(); // Apaga tudo primeiro
    apply_core_defaults();
    setAppDefaults();
    commit(); // Salva os valores padrão
}

esp_err_t NvsCore::erase_namespace()
{
    esp_err_t err = open_nvs(NVS_READWRITE);
    if (err != ESP_OK)
        return err;

    err = nvs_erase_all(_handle);
    if (err == ESP_OK) {
        err = nvs_commit(_handle);
    }

    close_nvs();
    return err;
}