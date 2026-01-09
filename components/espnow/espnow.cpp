#include "espnow.hpp"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "protocol_messages.hpp"
#include <algorithm>
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
EspNow::EspNow() {}

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
    }
    if (singleton_mutex_ != nullptr) {
        vSemaphoreDelete(singleton_mutex_);
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
    ESP_ERROR_CHECK(esp_now_add_peer(&broadcast_peer));

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
                       PayloadType payload_type,
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
        ESP_LOGE(TAG, "Could not find peer with node_id: %" PRIu8, static_cast<uint8_t>(dest_node_id));
        return ESP_ERR_NOT_FOUND;
    }

    size_t packet_len = sizeof(MessageHeader) + len;
    std::vector<uint8_t> packet_buffer(packet_len);

    MessageHeader *header    = reinterpret_cast<MessageHeader *>(packet_buffer.data());
    header->msg_type         = MessageType::DATA;
    header->sequence_number  = 0; // TODO: Implement sequence numbers
    header->sender_type      = config_.node_type;
    header->sender_node_id   = config_.node_id;
    header->payload_type     = payload_type;
    header->requires_ack     = require_ack;
    header->dest_node_id     = dest_node_id;
    header->timestamp_ms     = esp_timer_get_time() / 1000;

    memcpy(packet_buffer.data() + sizeof(MessageHeader), payload, len);

    return send_packet(dest_mac, packet_buffer.data(), packet_len);
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

esp_err_t EspNow::add_peer(NodeId node_id, const uint8_t *mac, uint8_t channel, NodeType type)
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

esp_err_t EspNow::start_pairing(uint32_t timeout_ms)
{
    if (is_pairing_active_) {
        ESP_LOGW(TAG, "Pairing is already active.");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Pairing started for %" PRIu32 " ms.", timeout_ms);

    if (pairing_timer_handle_ != nullptr) {
        xTimerDelete(pairing_timer_handle_, portMAX_DELAY);
        pairing_timer_handle_ = nullptr;
    }
    if (pairing_timeout_timer_handle_ != nullptr) {
        xTimerDelete(pairing_timeout_timer_handle_, portMAX_DELAY);
        pairing_timeout_timer_handle_ = nullptr;
    }

    if (config_.is_master) {
        pairing_timeout_timer_handle_ = xTimerCreate("pairing_timeout", pdMS_TO_TICKS(timeout_ms),
                                                     pdFALSE, this, pairing_timer_cb);
        if (pairing_timeout_timer_handle_ == nullptr) {
            ESP_LOGE(TAG, "Failed to create pairing timeout timer.");
            return ESP_FAIL;
        }
        xTimerStart(pairing_timeout_timer_handle_, 0);
    }
    else { // Slave
        pairing_timer_handle_ = xTimerCreate("pairing_periodic", pdMS_TO_TICKS(5000),
                                             pdTRUE, this, periodic_pairing_cb);
        pairing_timeout_timer_handle_ =
            xTimerCreate("pairing_timeout", pdMS_TO_TICKS(timeout_ms), pdFALSE, this,
                         pairing_timer_cb);

        if (pairing_timer_handle_ == nullptr || pairing_timeout_timer_handle_ == nullptr) {
            ESP_LOGE(TAG, "Failed to create slave pairing timers.");
            return ESP_FAIL;
        }
        xTimerStart(pairing_timer_handle_, 0);
        xTimerStart(pairing_timeout_timer_handle_, 0);
        send_pair_request();
    }

    is_pairing_active_ = true;
    return ESP_OK;
}

esp_err_t EspNow::remove_peer(NodeId node_id)
{
    esp_err_t result = ESP_FAIL;
    if (xSemaphoreTake(peers_mutex_, portMAX_DELAY) == pdTRUE) {
        result = remove_peer_internal(node_id);
        xSemaphoreGive(peers_mutex_);
    }
    return result;
}

// --- Private Methods ---
esp_err_t EspNow::send_packet(const uint8_t *mac_addr, const void *data, size_t len)
{
    if (len > ESP_NOW_MAX_DATA_LEN - CRC_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    std::vector<uint8_t> buffer(len + CRC_SIZE);
    memcpy(buffer.data(), data, len);
    buffer.back() = esp_rom_crc8_le(0, buffer.data(), len);

    esp_err_t result = esp_now_send(mac_addr, buffer.data(), buffer.size());
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send packet to %02X:%02X:%02X:%02X:%02X:%02X: %s",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4],
                 mac_addr[5], esp_err_to_name(result));
    }
    return result;
}

esp_err_t EspNow::remove_peer_internal(NodeId node_id)
{
    auto it = std::find_if(
        peers_.begin(), peers_.end(),
        [node_id](const PeerInfo &p) { return p.node_id == node_id; });

    if (it == peers_.end()) {
        ESP_LOGW(TAG, "Could not find peer with node_id: %" PRIu8,
                 static_cast<uint8_t>(node_id));
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t result = esp_now_del_peer(it->mac);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to remove peer from ESP-NOW: %s", esp_err_to_name(result));
    }

    ESP_LOGI(TAG, "Removed peer %02X:%02X:%02X:%02X:%02X:%02X (ID: %" PRIu8 ")",
             it->mac[0], it->mac[1], it->mac[2], it->mac[3], it->mac[4], it->mac[5],
             static_cast<uint8_t>(node_id));

    peers_.erase(it);
    return ESP_OK;
}

esp_err_t EspNow::add_peer_internal(NodeId node_id, const uint8_t *mac, uint8_t channel, NodeType type)
{
    for (auto it = peers_.begin(); it != peers_.end(); ++it) {
        if (it->node_id == node_id) {
            ESP_LOGI(TAG, "Node ID %" PRIu8 " already exists. Updating peer info.",
                     static_cast<uint8_t>(node_id));
            PeerInfo updated_peer  = *it;
            bool mac_changed     = (memcmp(updated_peer.mac, mac, 6) != 0);
            bool channel_changed = (updated_peer.channel != channel);

            if (mac_changed) {
                // Restore error handling for deleting the old peer
                esp_err_t del_result = esp_now_del_peer(updated_peer.mac);
                if (del_result != ESP_OK && del_result != ESP_ERR_ESPNOW_NOT_FOUND) {
                    ESP_LOGE(TAG, "Failed to remove old MAC for peer %" PRIu8 ": %s",
                             static_cast<uint8_t>(node_id), esp_err_to_name(del_result));
                }

                esp_now_peer_info_t peer_info = {};
                memcpy(peer_info.peer_addr, mac, 6);
                peer_info.channel = channel;
                peer_info.encrypt = false;
                ESP_ERROR_CHECK(esp_now_add_peer(&peer_info));
            }
            else if (channel_changed) {
                esp_now_peer_info_t peer_info = {};
                memcpy(peer_info.peer_addr, mac, 6);
                peer_info.channel = channel;
                peer_info.encrypt = false;
                ESP_ERROR_CHECK(esp_now_mod_peer(&peer_info));
            }

            memcpy(updated_peer.mac, mac, 6);
            updated_peer.type         = type;
            updated_peer.channel      = channel;
            updated_peer.last_seen_ms = esp_timer_get_time() / 1000;

            peers_.erase(it);
            peers_.insert(peers_.begin(), updated_peer);
            return ESP_OK;
        }
    }

    if (peers_.size() >= MAX_PEERS) {
        ESP_LOGW(TAG, "Peer list is full. Removing the oldest peer.");
        const PeerInfo &oldest_peer = peers_.back();
        esp_now_del_peer(oldest_peer.mac);
        peers_.pop_back();
    }

    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, mac, 6);
    peer_info.channel = channel;
    peer_info.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer_info));

    PeerInfo new_peer;
    memcpy(new_peer.mac, mac, 6);
    new_peer.node_id      = node_id;
    new_peer.type         = type;
    new_peer.channel      = channel;
    new_peer.last_seen_ms = esp_timer_get_time() / 1000;
    new_peer.paired       = true;
    peers_.insert(peers_.begin(), new_peer);

    ESP_LOGI(TAG, "New peer %02X:%02X:%02X:%02X:%02X:%02X (ID: %" PRIu8 ") added.",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], static_cast<uint8_t>(node_id));
    return ESP_OK;
}

// --- ESP-NOW Callbacks ---
void EspNow::esp_now_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
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
        // Log dropped packet
    }
}

void EspNow::esp_now_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    // Placeholder for ACK tracking
}

// --- Tasks ---
void EspNow::rx_dispatch_task(void *arg)
{
    EspNow *self = static_cast<EspNow *>(arg);
    RxPacket packet;

    for (;;) {
        if (xQueueReceive(self->rx_dispatch_queue_, &packet, portMAX_DELAY) == pdTRUE) {
            // CRC Validation
            if (packet.len < CRC_SIZE) {
                continue;
            }
            uint8_t received_crc   = packet.data[packet.len - 1];
            uint8_t calculated_crc = esp_rom_crc8_le(0, packet.data, packet.len - CRC_SIZE);

            if (received_crc != calculated_crc) {
                ESP_LOGW(TAG, "CRC mismatch from %02X:%02X:%02X:%02X:%02X:%02X",
                         packet.src_mac[0], packet.src_mac[1], packet.src_mac[2],
                         packet.src_mac[3], packet.src_mac[4], packet.src_mac[5]);
                continue;
            }

            if (packet.len < sizeof(MessageHeader) + CRC_SIZE) {
                continue;
            }
            const MessageHeader *header = reinterpret_cast<const MessageHeader *>(packet.data);

            switch (header->msg_type) {
            case MessageType::PAIR_REQUEST:
            case MessageType::PAIR_RESPONSE:
            case MessageType::HEARTBEAT:
            case MessageType::HEARTBEAT_RESPONSE:
                if (xQueueSend(self->transport_worker_queue_, &packet, 0) != pdTRUE) {
                    ESP_LOGW(TAG, "Transport worker queue full.");
                }
                break;
            case MessageType::DATA:
            case MessageType::ACK:
            case MessageType::COMMAND:
                if (xQueueSend(self->config_.app_rx_queue, &packet, 0) != pdTRUE) {
                    ESP_LOGW(TAG, "Application RX queue full.");
                }
                break;
            default:
                ESP_LOGW(TAG, "Unknown message type: 0x%02X", static_cast<int>(header->msg_type));
                break;
            }
        }
    }
}

void EspNow::send_pair_request()
{
    PairRequest request;
    request.header.msg_type       = MessageType::PAIR_REQUEST;
    request.header.sender_node_id = config_.node_id;
    request.header.sender_type    = config_.node_type;
    request.uptime_ms             = esp_timer_get_time() / 1000;

    const uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_err_t result = send_packet(broadcast_mac, &request, sizeof(request));
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "Sent pairing request.");
    }
    else {
        ESP_LOGE(TAG, "Failed to send pairing request: %s", esp_err_to_name(result));
    }
}

void EspNow::handle_pair_request(const RxPacket &packet)
{
    if (!config_.is_master || !is_pairing_active_) {
        return;
    }

    const PairRequest *request = reinterpret_cast<const PairRequest *>(packet.data);

    ESP_LOGI(TAG, "Received pair request from Node ID %" PRIu8, static_cast<uint8_t>(request->header.sender_node_id));

    PairResponse response;
    response.header.msg_type       = MessageType::PAIR_RESPONSE;
    response.header.sender_node_id = config_.node_id;
    response.header.sender_type    = config_.node_type;
    response.header.dest_node_id   = request->header.sender_node_id;

    // Check if the request is from another hub and reject it
    if (request->header.sender_type == NodeType::HUB) {
        ESP_LOGW(TAG, "Rejecting pairing request from another Hub");
        response.status = PairStatus::REJECTED_NOT_ALLOWED;
        send_packet(packet.src_mac, &response, sizeof(response));
        return;
    }

    // Add the peer to the internal list
    add_peer_internal(request->header.sender_node_id, packet.src_mac,
                      config_.wifi_channel, request->header.sender_type);

    // Send an accepted response
    response.status       = PairStatus::ACCEPTED;
    response.wifi_channel = config_.wifi_channel;
    send_packet(packet.src_mac, &response, sizeof(response));
}

void EspNow::handle_pair_response(const RxPacket &packet)
{
    // Ignore if we are the master or not in pairing mode
    if (config_.is_master || !is_pairing_active_) {
        return;
    }

    const PairResponse *response = reinterpret_cast<const PairResponse *>(packet.data);

    if (response->status == PairStatus::ACCEPTED) {
        ESP_LOGI(TAG, "Pairing accepted by Hub (Node ID %" PRIu8 ")", static_cast<uint8_t>(response->header.sender_node_id));

        // Use the channel provided by the hub for communication
        add_peer_internal(response->header.sender_node_id, packet.src_mac,
                          response->wifi_channel, response->header.sender_type);

        // Stop both pairing timers (periodic and timeout)
        if (pairing_timer_handle_ != nullptr) {
            xTimerDelete(pairing_timer_handle_, portMAX_DELAY);
            pairing_timer_handle_ = nullptr;
        }
        if (pairing_timeout_timer_handle_ != nullptr) {
            xTimerDelete(pairing_timeout_timer_handle_, portMAX_DELAY);
            pairing_timeout_timer_handle_ = nullptr;
        }
        is_pairing_active_ = false;
        ESP_LOGI(TAG, "Pairing successful.");
    }
    else {
        ESP_LOGW(TAG, "Pairing rejected by Hub.");
    }
}

void EspNow::transport_worker_task(void *arg)
{
    EspNow *self = static_cast<EspNow *>(arg);
    RxPacket packet;

    for (;;) {
        if (xQueueReceive(self->transport_worker_queue_, &packet, portMAX_DELAY) == pdTRUE) {
            if (packet.len < sizeof(MessageHeader) + CRC_SIZE) {
                continue;
            }
            const MessageHeader *header =
                reinterpret_cast<const MessageHeader *>(packet.data);

            switch (header->msg_type) {
            case MessageType::PAIR_REQUEST:
                self->handle_pair_request(packet);
                break;
            case MessageType::PAIR_RESPONSE:
                self->handle_pair_response(packet);
                break;
            case MessageType::HEARTBEAT:
                // self->handle_heartbeat(packet); // TODO
                break;
            default:
                break;
            }
        }
    }
}

void EspNow::pairing_timer_cb(TimerHandle_t xTimer)
{
    EspNow *self = static_cast<EspNow *>(pvTimerGetTimerID(xTimer));
    if (self == nullptr) {
        return;
    }

    if (self->config_.is_master) {
        // Master's pairing window has expired
        self->is_pairing_active_ = false;
        ESP_LOGI(TAG, "Pairing timeout reached. Pairing stopped.");
    }
    else {
        // Slave's pairing attempt has timed out
        ESP_LOGW(TAG, "Pairing attempt timed out.");
        if (self->pairing_timer_handle_ != nullptr) {
            xTimerDelete(self->pairing_timer_handle_, portMAX_DELAY);
            self->pairing_timer_handle_ = nullptr;
        }
        self->is_pairing_active_ = false;
    }
}

void EspNow::periodic_pairing_cb(TimerHandle_t xTimer)
{
    EspNow *self = static_cast<EspNow *>(pvTimerGetTimerID(xTimer));
    if (self != nullptr) {
        // Periodically send a pairing request
        self->send_pair_request();
    }
}
