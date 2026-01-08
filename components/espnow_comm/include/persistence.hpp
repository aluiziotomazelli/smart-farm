#pragma once

#include <cstdint>
#include <vector>
#include <cstddef> // Para size_t

namespace PeerPersistence
{
    // Estrutura para persistir informacoes basicas dos peers
    struct PersistentPeer
    {
        uint8_t mac[6];
        uint8_t node_id;
    };

    // Estrutura de armazenamento para RTC (com verificacao de integridade)
    struct RTCStorage
    {
        uint32_t crc;
        uint8_t count;
        PersistentPeer peers[20]; // MAX_PEERS deve ser consistente
    };

    // Inicializa a particao NVS
    void initNVS();

    // Salva a lista de peers na memoria RTC
    bool saveToRTC(const std::vector<PersistentPeer> &peers);

    // Carrega a lista de peers da memoria RTC
    std::vector<PersistentPeer> loadFromRTC();

    // Salva a lista de peers no NVS
    bool saveToNVS(const std::vector<PersistentPeer> &peers);

    // Carrega a lista de peers do NVS
    std::vector<PersistentPeer> loadFromNVS();

    // Funcoes internas para validacao
    bool isRTCDataValid();
    uint32_t calculateCRC(const RTCStorage &storage);

    // Constantes
    static constexpr const char *NVS_NAMESPACE = "espnow_peers";
    static constexpr const char *NVS_KEY       = "peers";
    static constexpr size_t MAX_PEERS          = 20;

} // namespace PeerPersistence
