#pragma once

#include "CoreTypes.hpp"
#include "esp_err.h"

class NvsCore
{
public:
    // Inicializa NVS e prepara acesso ao namespace
    static esp_err_t init();

    // Carrega CoreStorage do NVS para RAM
    static esp_err_t load();

    // Salva CoreStorage da RAM para NVS
    static esp_err_t commit();

    // Acesso ao core em RAM
    static CoreStorage &data();

    // Aplica defaults e salva
    static void factory_reset();

private:
    // Instância única em RAM
    static CoreStorage core_;

    // Aplica valores default para um core zerado
    static void apply_defaults();

    // Migração de versões antigas
    static esp_err_t migrate(uint32_t from_version);
};
