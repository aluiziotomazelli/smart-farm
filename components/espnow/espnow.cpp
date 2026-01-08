#include "espnow.hpp"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "protocol_messages.hpp"
#include <cstring>
#include <inttypes.h>

// Logging TAG
static const char *TAG = "EspNow";

// Singleton static members
EspNow *EspNow::instance_ptr_              = nullptr;
SemaphoreHandle_t EspNow::singleton_mutex_ = nullptr;

// --- Singleton ---
EspNow &EspNow::instance()
{
    if (instance_ptr_ == nullptr) {
        if (singleton_mutex_ == nullptr) {
            singleton_mutex_ = xSemaphoreCreateMutex();
        }

        if (xSemaphoreTake(singleton_mutex_, portMAX_DELAY) == pdTRUE) {
            if (instance_ptr_ == nullptr) {
                instance_ptr_ = new EspNow();
            }
            xSemaphoreGive(singleton_mutex_);
        }
    }
    return *instance_ptr_;
}

// --- Constructor & Destructor ---
EspNow::EspNow()
{
}

EspNow::~EspNow()
{
    if (rx_dispatch_task_handle_ != nullptr) {
        vTaskDelete(rx_dispatch_task_handle_);
    }
    if (transport_worker_task_handle_ != nullptr) {
        vTaskDelete(transport_worker_task_handle_);
    }

    if (rx_dispatch_queue_ != nullptr) {
        vQueueDelete(rx_dispatch_queue_);
    }
    if (transport_worker_queue_ != nullptr) {
        vQueueDelete(transport_worker_queue_);
    }

    if (is_initialized_) {
        esp_now_deinit();
    }

    if (peers_mutex_ != nullptr) {
        vSemaphoreDelete(peers_mutex_);
        peers_mutex_ = nullptr;
    }

    if (singleton_mutex_ != nullptr) {
        vSemaphoreDelete(singleton_mutex_);
        singleton_mutex_ = nullptr;
    }

    ESP_LOGI(TAG, "Resources released.");
}

// --- Public API ---
esp_err_t EspNow::init(const EspNowConfig &config)
{
    if (is_initialized_) {
        ESP_LOGW(TAG, "Already initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    if (config.app_rx_queue == nullptr) {
        ESP_LOGE(TAG, "Application RX queue cannot be null.");
        return ESP_ERR_INVALID_ARG;
    }

    config_ = config;

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(esp_now_recv_cb));
    ESP_ERROR_CHECK(esp_now_register_send_cb(esp_now_send_cb));

    esp_now_peer_info_t broadcast_peer = {};
    const uint8_t broadcast_mac[]      = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    memcpy(broadcast_peer.peer_addr, broadcast_mac, 6);
    broadcast_peer.channel = config_.wifi_channel;
    broadcast_peer.encrypt = false;
    if (esp_now_add_peer(&broadcast_peer) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add broadcast peer");
    }

    peers_mutex_ = xSemaphoreCreateMutex();
    if (peers_mutex_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create peers mutex.");
        return ESP_FAIL;
    }

    rx_dispatch_queue_      = xQueueCreate(20, sizeof(RxPacket));
    transport_worker_queue_ = xQueueCreate(10, sizeof(RxPacket));
    if (rx_dispatch_queue_ == nullptr || transport_worker_queue_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create internal queues.");
        return ESP_FAIL;
    }

    if (xTaskCreate(rx_dispatch_task, "espnow_dispatch", 2048, this, 10,
                    &rx_dispatch_task_handle_) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create dispatch task.");
        return ESP_FAIL;
    }

    if (xTaskCreate(transport_worker_task, "espnow_worker", 3072, this, 5,
                    &transport_worker_task_handle_) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create worker task.");
        return ESP_FAIL;
    }

    is_initialized_ = true;
    ESP_LOGI(TAG, "EspNow component initialized successfully.");
    return ESP_OK;
}

esp_err_t EspNow::send(NodeId dest_node_id,
                       const void *payload,
                       size_t len,
                       bool require_ack)
{
    if (!is_initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    if (payload == nullptr || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len > MAX_PAYLOAD_SIZE) {
        ESP_LOGE(TAG, "Payload exceeds maximum size of %d bytes.", MAX_PAYLOAD_SIZE);
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t dest_mac[6];
    bool peer_found = false;
    if (xSemaphoreTake(peers_mutex_, portMAX_DELAY) == pdTRUE) {
        for (const auto &peer : peers_) {
            if (peer.node_id == dest_node_id) {
                memcpy(dest_mac, peer.mac, 6);
                peer_found = true;
                break;
            }
        }
        xSemaphoreGive(peers_mutex_);
    }

    if (!peer_found) {
        ESP_LOGE(TAG, "Could not find peer with node_id: %" PRIu8,
                 static_cast<uint8_t>(dest_node_id));
        return ESP_ERR_NOT_FOUND;
    }

    size_t packet_len = sizeof(MessageHeader) + len + sizeof(uint8_t);
    std::vector<uint8_t> packet_buffer(packet_len);

    MessageHeader *header  = reinterpret_cast<MessageHeader *>(packet_buffer.data());
    header->msg_type       = MessageType::DATA;
    header->sender_node_id = config_.node_id;
    header->dest_node_id   = dest_node_id;
    header->requires_ack   = require_ack;

    memcpy(packet_buffer.data() + sizeof(MessageHeader), payload, len);

    uint8_t crc = esp_rom_crc8_le(0, packet_buffer.data(), sizeof(MessageHeader) + len);
    packet_buffer[packet_len - 1] = crc;

    esp_err_t result = esp_now_send(dest_mac, packet_buffer.data(), packet_len);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send data to node_id %" PRIu8 ": %s",
                 static_cast<uint8_t>(dest_node_id), esp_err_to_name(result));
    }
    return result;
}

std::vector<EspNow::PeerInfo> EspNow::get_peers()
{
    std::vector<PeerInfo> peers_copy;
    if (xSemaphoreTake(peers_mutex_, portMAX_DELAY) == pdTRUE) {
        peers_copy = peers_;
        xSemaphoreGive(peers_mutex_);
    }
    return peers_copy;
}

esp_err_t EspNow::add_peer(NodeId node_id,
                           const uint8_t *mac,
                           uint8_t channel,
                           NodeType type)
{
    if (mac == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = ESP_FAIL;
    if (xSemaphoreTake(peers_mutex_, portMAX_DELAY) == pdTRUE) {
        result = add_peer_internal(node_id, mac, channel, type);
        xSemaphoreGive(peers_mutex_);
    }
    return result;
}

// --- Private Methods ---
esp_err_t EspNow::add_peer_internal(NodeId node_id,
                                    const uint8_t *mac,
                                    uint8_t channel,
                                    NodeType type)
{
    for (auto it = peers_.begin(); it != peers_.end(); ++it) {
        if (it->node_id == node_id) {
            ESP_LOGI(TAG, "Node ID %" PRIu8 " already exists. Updating peer info.",
                     static_cast<uint8_t>(node_id));
            PeerInfo updated_peer = *it;

            bool mac_changed     = (memcmp(updated_peer.mac, mac, 6) != 0);
            bool channel_changed = (updated_peer.channel != channel);

            if (mac_changed) {
                esp_err_t del_result = esp_now_del_peer(updated_peer.mac);
                if (del_result != ESP_OK && del_result != ESP_ERR_ESPNOW_NOT_FOUND) {
                    ESP_LOGE(TAG, "Failed to remove old MAC for peer %" PRIu8 ": %s",
                             static_cast<uint8_t>(node_id), esp_err_to_name(del_result));
                    return del_result;
                }

                esp_now_peer_info_t peer_info = {};
                memcpy(peer_info.peer_addr, mac, 6);
                peer_info.channel    = channel;
                peer_info.encrypt    = false;
                esp_err_t add_result = esp_now_add_peer(&peer_info);
                if (add_result != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to add new MAC for peer %" PRIu8 ": %s",
                             static_cast<uint8_t>(node_id), esp_err_to_name(add_result));
                    return add_result;
                }
            }
            else if (channel_changed) {
                esp_now_peer_info_t peer_info = {};
                memcpy(peer_info.peer_addr, mac, 6);
                peer_info.channel    = channel;
                peer_info.encrypt    = false;
                esp_err_t mod_result = esp_now_mod_peer(&peer_info);
                if (mod_result != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to modify channel for peer %" PRIu8 ": %s",
                             static_cast<uint8_t>(node_id), esp_err_to_name(mod_result));
                    return mod_result;
                }
            }

            memcpy(updated_peer.mac, mac, 6);
            updated_peer.type         = type;
            updated_peer.channel      = channel;
            updated_peer.last_seen_ms = esp_timer_get_time() / 1000;

            peers_.erase(it);
            peers_.insert(peers_.begin(), updated_peer);

            ESP_LOGI(TAG,
                     "Peer with Node ID %" PRIu8
                     " updated to MAC %02X:%02X:%02X:%02X:%02X:%02X.",
                     static_cast<uint8_t>(node_id), mac[0], mac[1], mac[2], mac[3],
                     mac[4], mac[5]);
            return ESP_OK;
        }
    }

    ESP_LOGI(TAG, "Node ID %" PRIu8 " not found. Adding as a new peer.",
             static_cast<uint8_t>(node_id));

    if (peers_.size() >= MAX_PEERS) {
        ESP_LOGW(TAG, "Peer list is full. Removing the oldest peer.");
        const PeerInfo &oldest_peer = peers_.back();
        esp_err_t result            = esp_now_del_peer(oldest_peer.mac);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "Failed to remove oldest peer from ESP-NOW: %s",
                     esp_err_to_name(result));
        }
        peers_.pop_back();
    }

    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, mac, 6);
    peer_info.channel = channel;
    peer_info.encrypt = false;
    esp_err_t result  = esp_now_add_peer(&peer_info);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add new peer to ESP-NOW: %s", esp_err_to_name(result));
        return result;
    }

    PeerInfo new_peer;
    memcpy(new_peer.mac, mac, 6);
    new_peer.node_id      = node_id;
    new_peer.type         = type;
    new_peer.channel      = channel;
    new_peer.last_seen_ms = esp_timer_get_time() / 1000;
    new_peer.paired       = true;
    peers_.insert(peers_.begin(), new_peer);

    ESP_LOGI(TAG, "New peer %02X:%02X:%02X:%02X:%02X:%02X (ID: %" PRIu8 ") added.",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
             static_cast<uint8_t>(node_id));
    return ESP_OK;
}

// --- ESP-NOW Callbacks ---
void EspNow::esp_now_recv_cb(const esp_now_recv_info_t *info,
                             const uint8_t *data,
                             int len)
{
    if (info == nullptr || data == nullptr || len <= 0 || len > ESP_NOW_MAX_DATA_LEN) {
        return;
    }

    RxPacket packet;
    memcpy(packet.src_mac, info->src_addr, 6);
    memcpy(packet.data, data, len);
    packet.len          = len;
    packet.rssi         = info->rx_ctrl->rssi;
    packet.timestamp_us = esp_timer_get_time();

    if (xQueueSendFromISR(instance_ptr_->rx_dispatch_queue_, &packet, 0) != pdTRUE) {
        // Log dropped packet if needed (from a non-ISR context)
    }
}

void EspNow::esp_now_send_cb(const esp_now_send_info_t *tx_info,
                             esp_now_send_status_t status)

{
    // Placeholder for send callback logic. Can be used for ACK tracking.
}

// --- Tasks ---
void EspNow::rx_dispatch_task(void *arg)
{
    EspNow *self = static_cast<EspNow *>(arg);
    RxPacket packet;

    for (;;) {
        if (xQueueReceive(self->rx_dispatch_queue_, &packet, portMAX_DELAY) == pdTRUE) {
            if (packet.len < sizeof(MessageHeader)) {
                continue;
            }
            const MessageHeader *header =
                reinterpret_cast<const MessageHeader *>(packet.data);

            switch (header->msg_type) {
            case MessageType::PAIR_REQUEST:
            case MessageType::PAIR_RESPONSE:
            case MessageType::HEARTBEAT:
            case MessageType::HEARTBEAT_RESPONSE:
                if (xQueueSend(self->transport_worker_queue_, &packet,
                               pdMS_TO_TICKS(10)) != pdTRUE) {
                    ESP_LOGW(TAG, "Transport worker queue full.");
                }
                break;
            case MessageType::DATA:
            case MessageType::ACK:
            case MessageType::COMMAND:
                if (xQueueSend(self->config_.app_rx_queue, &packet, pdMS_TO_TICKS(10)) !=
                    pdTRUE) {
                    ESP_LOGW(TAG, "Application RX queue full.");
                }
                break;
            default:
                ESP_LOGW(TAG, "Unknown message type: 0x%02X",
                         static_cast<int>(header->msg_type));
                break;
            }
        }
    }
}

void EspNow::transport_worker_task(void *arg)
{
    EspNow *self = static_cast<EspNow *>(arg);
    RxPacket packet;

    for (;;) {
        if (xQueueReceive(self->transport_worker_queue_, &packet, portMAX_DELAY) ==
            pdTRUE) {
            self->process_transport_message(packet);
        }
    }
}

void EspNow::process_transport_message(const RxPacket &packet)
{
    if (packet.len < sizeof(MessageHeader)) {
        return;
    }
    const MessageHeader *header = reinterpret_cast<const MessageHeader *>(packet.data);
    ESP_LOGD(TAG, "Processing transport message type %d",
             static_cast<int>(header->msg_type));
}
