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

    // Desinicializa o ESP-NOW
    if (is_initialized_)
    {
        esp_now_deinit();
    }

    // Deleta o mutex do singleton
    if (singleton_mutex_ != nullptr)
    {
        vSemaphoreDelete(singleton_mutex_);
        singleton_mutex_ = nullptr;
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

esp_err_t EspNow::send(const uint8_t *dest_mac, const void *data, size_t len)
{
    if (!is_initialized_)
    {
        return ESP_ERR_INVALID_STATE;
    }
    // Placeholder para a logica de envio
    return esp_now_send(dest_mac, static_cast<const uint8_t *>(data), len);
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

bool EspNow::is_peer_online(const uint8_t *mac)
{
    // Placeholder
    return false;
}

const uint8_t *EspNow::find_peer_mac(NodeType type, uint8_t id)
{
    // Placeholder
    return nullptr;
}
