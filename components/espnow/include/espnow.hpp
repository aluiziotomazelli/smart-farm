#pragma once

#include "esp_now.h"
#include "protocol_types.hpp"
#include <cstdint>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// Configuracao para inicializar o componente EspNow
struct EspNowConfig
{
    uint8_t node_id;            // ID unico deste no
    NodeType node_type;         // Tipo deste no (Hub, WaterTank, etc)
    QueueHandle_t app_rx_queue; // Fila para onde as mensagens da aplicacao serao enviadas
    uint8_t wifi_channel;       // Canal WiFi inicial
    uint32_t ack_timeout_ms;    // Timeout para ACK logico
    uint32_t heartbeat_interval_ms; // Intervalo de heartbeat
    bool is_master;                 // Se este no e o mestre da rede (Hub)

    // Construtor com valores padrao
    EspNowConfig()
        : node_id(0)
        , node_type(NodeType::UNKNOWN)
        , app_rx_queue(nullptr)
        , wifi_channel(DEFAULT_WIFI_CHANNEL)
        , ack_timeout_ms(DEFAULT_ACK_TIMEOUT_MS)
        , heartbeat_interval_ms(DEFAULT_HEARTBEAT_INTERVAL_MS)
        , is_master(false)
    {
    }
};

// Classe principal para comunicacao ESP-NOW
// Gerencia pareamento, heartbeats e a troca de mensagens de forma assincrona.
class EspNow
{
public:
    // Singleton
    static EspNow &instance();

    // Impede copias
    EspNow(const EspNow &)            = delete;
    EspNow &operator=(const EspNow &) = delete;

    // Destrutor para liberar recursos
    ~EspNow();

    // Pacote recebido (estrutura generica usada nas filas)
    struct RxPacket
    {
        uint8_t src_mac[6];
        uint8_t data[ESP_NOW_MAX_DATA_LEN];
        size_t len;
        int8_t rssi;
        int64_t timestamp_us;
    };

    // API publica
    esp_err_t init(const EspNowConfig &config);
    esp_err_t send(const uint8_t *dest_mac, const void *data, size_t len);

    // Funcoes de gerenciamento de peers (a serem implementadas)
    bool is_peer_online(const uint8_t *mac);
    const uint8_t *find_peer_mac(NodeType type, uint8_t mode_id);

private:
    // Construtor privado para o singleton
    EspNow();

    // Informacao interna de um peer
    struct PeerInfo
    {
        uint8_t mac[6];
        NodeType type;
        uint8_t node_id;
        uint32_t last_seen_ms;
        bool paired;
    };

    // --- Membros Privados ---
    EspNowConfig config_{};
    std::vector<PeerInfo> peers_;
    bool is_initialized_ = false;

    // Filas para a arquitetura Dispatcher-Worker
    QueueHandle_t rx_dispatch_queue_      = nullptr; // Fila unica para o ISR
    QueueHandle_t transport_worker_queue_ = nullptr; // Fila para a task de protocolo

    // Handles das tasks
    TaskHandle_t rx_dispatch_task_handle_      = nullptr;
    TaskHandle_t transport_worker_task_handle_ = nullptr;

    // Singleton
    static EspNow *instance_ptr_;
    static SemaphoreHandle_t singleton_mutex_;

    // --- Metodos Privados ---

    // Processa mensagens de protocolo (PAIR, HEARTBEAT)
    void process_transport_message(const RxPacket &packet);
    void handle_pair_request(const RxPacket &packet);
    void handle_heartbeat(const RxPacket &packet);

    // Funcoes das tasks
    static void rx_dispatch_task(void *arg);
    static void transport_worker_task(void *arg);

    // Callback estatico do ESP-NOW (contexto ISR)
    static void esp_now_recv_cb(const esp_now_recv_info_t *info,
                                const uint8_t *data,
                                int len);
};
