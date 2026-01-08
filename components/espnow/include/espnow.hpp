#pragma once
#include "protocol_types.h" // Apenas enums básicos
#include <cstdint>
#include <functional>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

class EspNow
{
public:
    // Pacote recebido (genérico)
    struct RxPacket
    {
        uint8_t src_mac[6];
        uint8_t data[MAX_PAYLOAD_SIZE];
        size_t len;
        int8_t rssi;
        int64_t timestamp_us;
    };

    // Callback para aplicação (dados brutos)
    using RxCallback = std::function<void(const RxPacket &packet)>;

    // API pública
    esp_err_t init();
    esp_err_t send(const uint8_t *dest_mac, const void *data, size_t len);
    void register_rx_callback(RxCallback callback);

    // Status de peers
    bool is_peer_online(const uint8_t *mac);
    const uint8_t *find_peer_mac(NodeType type, uint8_t id);

private:
    // Informação interna de peer
    struct PeerInfo
    {
        uint8_t mac[6];
        NodeType type;
        uint8_t id;
        uint32_t last_seen_ms;
        bool paired;
    };

    QueueHandle_t rx_queue_transport_;
    QueueHandle_t rx_queue_app_;
    RxCallback app_callback_;

    std::vector<PeerInfo> peers_;

    // Processa mensagens de transport (PAIR, HEARTBEAT)
    void process_transport_message(const RxPacket &packet);
    void handle_pair_request(const RxPacket &packet);
    void handle_heartbeat(const RxPacket &packet);

    // Tasks
    static void transport_rx_task(void *arg);
    static void app_rx_task(void *arg);

    // Callback ESP-NOW
    static void esp_now_recv_cb(const esp_now_recv_info_t *info,
                                const uint8_t *data,
                                int len);
};