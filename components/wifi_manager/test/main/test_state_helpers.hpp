// test_state_helpers.hpp
#pragma once

#ifdef UNIT_TEST

#include "wifi_manager.hpp"
#include <functional>

class TestStateHelpers
{
public:
    // Função para forçar um estado específico simulando eventos
    static bool forceState(WiFiManager &wm, WiFiManager::State target_state);

    // Helper para executar comando e verificar resultado
    static void testCommand(WiFiManager &wm,
                            WiFiManagerTestAccessor &accessor,
                            const char *command_name,
                            std::function<esp_err_t()> send_command,
                            bool expected_success);

    // Mapear estado para string
    static const char *stateToString(WiFiManager::State state);

    // Lista completa de estados
    static const std::vector<WiFiManager::State> getAllStates();
};

#endif // UNIT_TEST