#include "espnow.hpp"
#include "protocol_messages.hpp"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include <cstring>

// Definicao do TAG para logging
static const char *TAG = "EspNow";

// Inicializacao dos membros estaticos do singleton
EspNow *EspNow::instance_ptr_         = nullptr;
SemaphoreHandle_t EspNow::singleton_mutex_ = nullptr;

// --- Singleton ---
EspNow &EspNow::instance()
{
    // A primeira verificacao nao e thread-safe, mas evita o bloqueio do mutex a cada chamada
    if (instance_ptr_ == nullptr)
    {
        // Cria o mutex na primeira chamada para garantir que a instancia seja criada de forma segura
        if (singleton_mutex_ == nullptr)
        {
            singleton_mutex_ = xSemaphoreCreateMutex();
        }

        // Bloqueia o mutex para criar a instancia
        if (xSemaphoreTake(singleton_mutex_, portMAX_DELAY) == pdTRUE)
        {
            if (instance_ptr_ == nullptr)
            {
                instance_ptr_ = new EspNow();
            }
            xSemaphoreGive(singleton_mutex_);
        }
    }
    return *instance_ptr_;
}

// --- Construtor e Destrutor ---
EspNow::EspNow()
{
    // O construtor e privado e a inicializacao principal ocorre no init()
}

EspNow::~EspNow()
{
    // Deleta as tasks
    if (rx_dispatch_task_handle_ != nullptr)
    {
        vTaskDelete(rx_dispatch_task_handle_);
    }
    if (transport_worker_task_handle_ != nullptr)
    {
        vTaskDelete(transport_worker_task_handle_);
    }

    // Deleta as filas
    if (rx_dispatch_queue_ != nullptr)
    {
        vQueueDelete(rx_dispatch_queue_);
    }
    if (transport_worker_queue_ != nullptr)
    {
        vQueueDelete(transport_worker_queue_);
    }

    // Deleta os mutexes
    if (peers_mutex_ != nullptr)
    {
        vSemaphoreDelete(peers_mutex_);
        peers_mutex_ = nullptr;
    }

    // Deleta o mutex do singleton
    if (singleton_mutex_ != nullptr)
    {
        vSemaphoreDelete(singleton_mutex_);
        singleton_mutex_ = nullptr;
    }

    // Desinicializa o ESP-NOW
    if (is_initialized_)
    {
        esp_now_deinit();
    }

    ESP_LOGI(TAG, "Recursos liberados.");
}

// --- API Publica ---
esp_err_t EspNow::init(const EspNowConfig &config)
{
    if (is_initialized_)
    {
        ESP_LOGW(TAG, "Ja inicializado.");
        return ESP_ERR_INVALID_STATE;
    }

    // Valida a configuracao
    if (config.app_rx_queue == nullptr)
    {
        ESP_LOGE(TAG, "A fila da aplicacao (app_rx_queue) nao pode ser nula.");
        return ESP_ERR_INVALID_ARG;
    }

    config_ = config;

    // Inicializa o ESP-NOW
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(esp_now_recv_cb));

    // Registra o peer de broadcast para garantir que sempre possamos enviar
    esp_now_peer_info_t broadcast_peer = {};
    const uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    memcpy(broadcast_peer.peer_addr, broadcast_mac, 6);
    broadcast_peer.channel = config_.wifi_channel;
    broadcast_peer.encrypt = false;

    if (esp_now_add_peer(&broadcast_peer) != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao adicionar o peer de broadcast");
        // Nao retornamos falha aqui, pois a comunicacao unicast ainda pode funcionar
    }

    // Cria os mutexes
    peers_mutex_ = xSemaphoreCreateMutex();
    if (peers_mutex_ == nullptr)
    {
        ESP_LOGE(TAG, "Falha ao criar o mutex dos peers.");
        return ESP_FAIL;
    }

    // Cria as filas internas
    rx_dispatch_queue_ = xQueueCreate(20, sizeof(RxPacket));
    transport_worker_queue_ = xQueueCreate(10, sizeof(RxPacket));
    if (rx_dispatch_queue_ == nullptr || transport_worker_queue_ == nullptr)
    {
        ESP_LOGE(TAG, "Falha ao criar filas internas.");
        return ESP_FAIL;
    }

    // Cria as tasks
    BaseType_t result;
    result = xTaskCreate(rx_dispatch_task,
                         "espnow_dispatch",
                         2048,
                         this,
                         10, // Prioridade alta para despachar rapido
                         &rx_dispatch_task_handle_);
    if (result != pdPASS)
    {
        ESP_LOGE(TAG, "Falha ao criar a task de dispatch.");
        return ESP_FAIL;
    }

    result = xTaskCreate(transport_worker_task,
                         "espnow_worker",
                         3072,
                         this,
                         5, // Prioridade normal para processamento
                         &transport_worker_task_handle_);
    if (result != pdPASS)
    {
        ESP_LOGE(TAG, "Falha ao criar a task do worker.");
        return ESP_FAIL;
    }

    is_initialized_ = true;
    ESP_LOGI(TAG, "Componente EspNow inicializado com sucesso.");
    return ESP_OK;
}

#include "esp_rom_crc.h"

esp_err_t EspNow::send(uint8_t dest_node_id, const void *payload, size_t len, bool require_ack)
{
    if (!is_initialized_)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (payload == nullptr || len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (len > MAX_PAYLOAD_SIZE)
    {
        ESP_LOGE(TAG, "Payload excede o tamanho maximo de %d bytes.", MAX_PAYLOAD_SIZE);
        return ESP_ERR_INVALID_ARG;
    }

    // 1. Encontra o MAC do peer de destino
    uint8_t dest_mac[6];
    bool peer_found = false;
    if (xSemaphoreTake(peers_mutex_, portMAX_DELAY) == pdTRUE)
    {
        for (const auto &peer : peers_)
        {
            if (peer.node_id == dest_node_id)
            {
                memcpy(dest_mac, peer.mac, 6);
                peer_found = true;
                break;
            }
        }
        xSemaphoreGive(peers_mutex_);
    }

    if (!peer_found)
    {
        ESP_LOGE(TAG, "Nao foi possivel encontrar o peer com node_id: %d", dest_node_id);
        return ESP_ERR_NOT_FOUND;
    }

    // 2. Monta o pacote completo
    size_t packet_len = sizeof(MessageHeader) + len + sizeof(uint8_t); // Header + Payload + CRC
    std::vector<uint8_t> packet_buffer(packet_len);

    MessageHeader *header = reinterpret_cast<MessageHeader *>(packet_buffer.data());
    header->msg_type = MessageType::DATA;
    header->sender_id = config_.node_id;
    header->requires_ack = require_ack;

    // 3. Copia o payload
    memcpy(packet_buffer.data() + sizeof(MessageHeader), payload, len);

    // 4. Calcula e adiciona o CRC
    uint8_t crc = esp_rom_crc8_le(0, packet_buffer.data(), sizeof(MessageHeader) + len);
    packet_buffer[packet_len - 1] = crc;

    // 5. Envia o pacote
    esp_err_t result = esp_now_send(dest_mac, packet_buffer.data(), packet_len);
    if (result != ESP_OK)
    {
        ESP_LOGE(TAG, "Falha ao enviar dados para o node_id %d: %s", dest_node_id, esp_err_to_name(result));
    }

    return result;
}

esp_err_t EspNow::add_peer(uint8_t node_id, const uint8_t *mac, uint8_t channel, NodeType type)
{
    if (mac == nullptr)
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = ESP_FAIL;
    if (xSemaphoreTake(peers_mutex_, portMAX_DELAY) == pdTRUE)
    {
        result = add_peer_internal(node_id, mac, channel, type);
        xSemaphoreGive(peers_mutex_);
    }
    return result;
}

// --- Metodos Privados ---

esp_err_t EspNow::add_peer_internal(uint8_t node_id, const uint8_t *mac, uint8_t channel, NodeType type)
{
    // 1. Procura por um peer existente com o mesmo node_id
    for (auto it = peers_.begin(); it != peers_.end(); ++it)
    {
        if (it->node_id == node_id)
        {
            ESP_LOGI(TAG, "Node ID %d ja existe. Atualizando informacoes do peer.", node_id);
            PeerInfo updated_peer = *it; // Cria uma copia para evitar invalidacao do iterador

            bool mac_changed = (memcmp(updated_peer.mac, mac, 6) != 0);

            if (mac_changed)
            {
                // Remove o MAC antigo da camada ESP-IDF
                esp_err_t del_result = esp_now_del_peer(updated_peer.mac);
                if (del_result != ESP_OK && del_result != ESP_ERR_ESPNOW_NOT_FOUND)
                {
                    ESP_LOGE(TAG, "Falha ao remover o MAC antigo do peer %d: %s", node_id, esp_err_to_name(del_result));
                    return del_result;
                }

                // Adiciona o novo MAC na camada ESP-IDF
                esp_now_peer_info_t peer_info = {};
                memcpy(peer_info.peer_addr, mac, 6);
                peer_info.channel = channel;
                peer_info.encrypt = false;
                esp_err_t add_result = esp_now_add_peer(&peer_info);
                if (add_result != ESP_OK)
                {
                    ESP_LOGE(TAG, "Falha ao adicionar o novo MAC para o peer %d: %s", node_id, esp_err_to_name(add_result));
                    return add_result;
                }
            }

            // Atualiza a informacao do peer na nossa copia
            memcpy(updated_peer.mac, mac, 6);
            updated_peer.type = type;
            updated_peer.channel = channel;
            updated_peer.last_seen_ms = esp_timer_get_time() / 1000;

            // Move o peer atualizado para o inicio da lista (mais recente)
            peers_.erase(it);
            peers_.insert(peers_.begin(), updated_peer);

            ESP_LOGI(TAG, "Peer com Node ID %d atualizado para MAC %02X:%02X:%02X:%02X:%02X:%02X.", node_id, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            return ESP_OK;
        }
    }

    // --- Se o node_id nao foi encontrado, adiciona como um novo peer ---
    ESP_LOGI(TAG, "Node ID %d nao encontrado. Adicionando como novo peer.", node_id);

    // Verifica se a lista de peers esta cheia
    if (peers_.size() >= MAX_PEERS)
    {
        ESP_LOGW(TAG, "Lista de peers cheia. Removendo o mais antigo.");
        const PeerInfo &oldest_peer = peers_.back();
        esp_err_t result = esp_now_del_peer(oldest_peer.mac);
        if (result != ESP_OK)
        {
            ESP_LOGE(TAG, "Falha ao remover o peer mais antigo do ESP-NOW: %s", esp_err_to_name(result));
        }
        peers_.pop_back();
    }

    // Adiciona o novo peer na camada ESP-IDF
    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, mac, 6);
    peer_info.channel = channel;
    peer_info.encrypt = false;
    esp_err_t result = esp_now_add_peer(&peer_info);

    if (result != ESP_OK)
    {
        ESP_LOGE(TAG, "Falha ao adicionar novo peer no ESP-NOW: %s", esp_err_to_name(result));
        return result;
    }

    // Adiciona o novo peer na nossa lista interna (no inicio)
    PeerInfo new_peer;
    memcpy(new_peer.mac, mac, 6);
    new_peer.node_id = node_id;
    new_peer.type = type;
    new_peer.channel = channel;
    new_peer.last_seen_ms = esp_timer_get_time() / 1000;
    new_peer.paired = true;

    peers_.insert(peers_.begin(), new_peer);

    ESP_LOGI(TAG, "Novo peer %02X:%02X:%02X:%02X:%02X:%02X (ID: %d) adicionado.", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], node_id);

    return ESP_OK;
}

// --- Callback do ESP-NOW (ISR) ---
void EspNow::esp_now_recv_cb(const esp_now_recv_info_t *info,
                             const uint8_t *data,
                             int len)
{
    if (info == nullptr || data == nullptr || len <= 0 ||
        len > ESP_NOW_MAX_DATA_LEN)
    {
        return;
    }

    // Prepara o pacote para a fila
    RxPacket packet;
    memcpy(packet.src_mac, info->src_addr, 6);
    memcpy(packet.data, data, len);
    packet.len            = len;
    packet.rssi           = info->rx_ctrl->rssi;
    packet.timestamp_us = esp_timer_get_time();

    // Envia para a fila de dispatch (ISR-safe)
    BaseType_t high_task_woken = pdFALSE;
    if (xQueueSendFromISR(instance_ptr_->rx_dispatch_queue_, &packet, &high_task_woken) !=
        pdTRUE)
    {
        // Nao usar ESP_LOG aqui (ISR context)
    }

    if (high_task_woken)
    {
        portYIELD_FROM_ISR();
    }
}

// --- Tasks ---
void EspNow::rx_dispatch_task(void *arg)
{
    EspNow *self = static_cast<EspNow *>(arg);
    RxPacket packet;

    for (;;)
    {
        if (xQueueReceive(self->rx_dispatch_queue_, &packet, portMAX_DELAY) == pdTRUE)
        {
            // Le o tipo da mensagem
            if (packet.len < sizeof(MessageHeader))
            {
                continue; // Pacote invalido
            }
            const MessageHeader *header = reinterpret_cast<const MessageHeader *>(packet.data);

            // Despacha para a fila correta
            switch (header->msg_type)
            {
                case MessageType::PAIR_REQUEST:
                case MessageType::PAIR_RESPONSE:
                case MessageType::HEARTBEAT:
                case MessageType::HEARTBEAT_RESPONSE:
                    // Mensagem de protocolo, vai para o worker interno
                    if (xQueueSend(self->transport_worker_queue_,
                                   &packet,
                                   pdMS_TO_TICKS(10)) != pdTRUE)
                    {
                        ESP_LOGW(TAG, "Fila do worker cheia.");
                    }
                    break;

                case MessageType::DATA:
                case MessageType::ACK:
                case MessageType::COMMAND:
                    // Mensagem da aplicacao, vai para a fila do app
                    if (xQueueSend(self->config_.app_rx_queue,
                                   &packet,
                                   pdMS_TO_TICKS(10)) != pdTRUE)
                    {
                        ESP_LOGW(TAG, "Fila da aplicacao cheia.");
                    }
                    break;

                default:
                    ESP_LOGW(TAG, "Tipo de mensagem desconhecido: 0x%02X", (int)header->msg_type);
                    break;
            }
        }
    }
}

void EspNow::transport_worker_task(void *arg)
{
    EspNow *self = static_cast<EspNow *>(arg);
    RxPacket packet;

    for (;;)
    {
        if (xQueueReceive(self->transport_worker_queue_, &packet, portMAX_DELAY) ==
            pdTRUE)
        {
            // Placeholder para a logica de processamento
            ESP_LOGD(TAG, "Worker recebeu um pacote da fila de transporte.");
            self->process_transport_message(packet);
        }
    }
}

// --- Metodos Privados (Placeholders) ---
void EspNow::process_transport_message(const RxPacket &packet)
{
     if (packet.len < sizeof(MessageHeader))
    {
        return;
    }
    const MessageHeader *header = reinterpret_cast<const MessageHeader *>(packet.data);

    ESP_LOGI(TAG, "Processando mensagem de transporte tipo %d", (int)header->msg_type);
}

