#pragma once
#include "CoreTypes.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

class NvsCore
{
protected:
    // Dados comuns (acessível pelo filho)
    CoreStorage core_;

    // Handle do NVS (mantido aberto durante load/commit para eficiência)
    nvs_handle_t _handle = 0;
    bool         _isOpen = false;

    // Helpers para o filho usar
    template <typename T> esp_err_t saveStruct(const char *key, const T &data)
    {
        if (!_isOpen)
            return ESP_FAIL;
        return nvs_set_blob(_handle, key, &data, sizeof(T));
    }

    template <typename T> esp_err_t loadStruct(const char *key, T &data)
    {
        if (!_isOpen)
            return ESP_FAIL;
        size_t required_size = sizeof(T);
        // Tenta ler. Se tamanho não bater ou não existir, retorna erro
        esp_err_t err = nvs_get_blob(_handle, key, &data, &required_size);
        if (err == ESP_OK && required_size != sizeof(T))
            return ESP_ERR_NVS_INVALID_LENGTH;
        return err;
    }

    // Métodos virtuais que o App DEVE implementar
    virtual esp_err_t loadAppData()    = 0;
    virtual esp_err_t saveAppData()    = 0;
    virtual void      setAppDefaults() = 0;

private:
    void      apply_core_defaults();
    esp_err_t open_nvs(nvs_open_mode_t mode);
    void      close_nvs();

public:
    NvsCore();
    virtual ~NvsCore() = default;

    // Inicializa partição
    esp_err_t init_partition();

    // Fluxo mestre: Carrega Core + App
    esp_err_t load();

    // Fluxo mestre: Salva Core + App
    esp_err_t commit();

    // Acesso aos dados comuns
    CoreStorage &getCoreData() { return core_; }

    // Factory reset completo
    void factory_reset();
};