#pragma once

#include "NvsCore.hpp"        // Sua classe base refatorada
#include "WaterTankStats.hpp" // Onde está a struct WaterTankStats

class WaterTankNvs : public NvsCore
{
public:
    WaterTankStats stats; // Memória em RAM para os dados do tanque

    // Métodos específicos de negócio (opcional, mas recomendado para encapsulamento)
    void updateStatus(uint16_t permille, float distance_cm, UsQuality quality, UsFailure failure);

protected:
    // --- Implementação dos Hooks Virtuais do NvsCore ---

    // Carrega a struct WaterTankStats
    esp_err_t loadAppData() override;

    // Salva a struct WaterTankStats
    esp_err_t saveAppData() override;

    // Define valores iniciais se o NVS estiver vazio ou corrompido
    void setAppDefaults() override;
};