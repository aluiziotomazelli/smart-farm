#pragma once

#include "interfaces/i_nvs_core.hpp"
#include "interfaces/i_hal_nvs.hpp"
#include "core_types.hpp"
#include "esp_err.h"

/**
 * @class NvsCore
 * @brief Base class for NVS persistence management.
 *
 * This class provides a common interface and helper methods for saving and loading
 * data to/from NVS, while decoupling from the hardware via IHalNvs.
 */
class NvsCore : public INvsCore
{
protected:
    IHalNvs    &hal_; ///< Reference to the NVS Hardware Abstraction Layer.
    CoreStorage core_;

    // Handle do NVS (mantido aberto durante load/commit para eficiência)
    nvs_handle_t _handle = 0;
    bool         _isOpen = false;
    const char  *_namespace;

    // Helpers para o filho usar
    template <typename T> esp_err_t saveStruct(const char *key, const T &data)
    {
        if (!_isOpen)
            return ESP_FAIL;
        return hal_.hal_nvs_set_blob(_handle, key, &data, sizeof(T));
    }

    template <typename T> esp_err_t loadStruct(const char *key, T &data)
    {
        if (!_isOpen)
            return ESP_FAIL;
        size_t required_size = sizeof(T);
        // Tenta ler. Se tamanho não bater ou não existir, retorna erro
        esp_err_t err = hal_.hal_nvs_get_blob(_handle, key, &data, &required_size);
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
    /**
     * @brief Construct a new NvsCore object.
     * @param ns The NVS namespace to use.
     * @param hal Reference to the IHalNvs implementation.
     */
    NvsCore(const char *ns, IHalNvs &hal);
    virtual ~NvsCore() override = default;

    // Inicializa partição
    esp_err_t init_partition() override;

    // Fluxo mestre: Carrega Core + App
    esp_err_t load() override;

    // Fluxo mestre: Salva Core + App
    esp_err_t commit() override;

    // Acesso aos dados comuns
    CoreStorage &getCoreData()
    {
        return core_;
    }

    // Factory reset completo (apaga apenas o namespace)
    void factory_reset() override;

    // Apaga tudo no namespace
    esp_err_t erase_namespace() override;

public:
    template <typename T> esp_err_t loadStructPublic(const char *key, T &data)
    {
        return loadStruct(key, data); // chama o protected
    }

    template <typename T> esp_err_t saveStructPublic(const char *key, const T &data)
    {
        return saveStruct(key, data);
    }
};