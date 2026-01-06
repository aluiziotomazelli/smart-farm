// espnow_comm.cpp
#include "espnow_comm.hpp"
#include "esp_mac.h"
#include "esp_rom_crc.h"
#include "esp_wifi.h" // For esp_wifi_set_protocol
#include "freertos/task.h"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"
#include "esp_timer.h"
#include <cstring> // For memset, memcpy, etc.

static const char *TAG = "EspNowComm";

// Initialize the static instance pointer for C-style callbacks.
EspNowComm *EspNowComm::instance_ = nullptr;

EspNowComm::EspNowComm(bool enable_persistence)
    : node_id_(0)
    , initialized_(false)
    , persistence_enabled_(enable_persistence)
    , discovery_active_(false)
    , discovery_end_time_(0)
    , mutex_(nullptr)
    , task_handle_(nullptr)
    , event_queue_(nullptr)
{
    memset(last_error_, 0, sizeof(last_error_));

    if (persistence_enabled_) {
        PeerPersistence::initNVS();
    }

    mutex_ = xSemaphoreCreateMutex();
}

EspNowComm::~EspNowComm()
{
    deinit(); // deinit already handles task and queue deletion
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
    }
}

bool EspNowComm::init(const ESPNOWConfig &config)
{
    if (initialized_) {
        strncpy(last_error_, "Already initialized", sizeof(last_error_) - 1);
        return false;
    }

    config_    = config;
    node_id_   = config.node_id;
    node_type_ = config.node_type;

    ESP_LOGI(TAG, "Initializing ESP-NOW service...");
    ESP_ERROR_CHECK(esp_now_init());

    ESP_ERROR_CHECK(esp_now_register_recv_cb(espNowRecvCb));
    ESP_ERROR_CHECK(esp_now_register_send_cb(espNowSendCb));

    if (config_.enable_encryption) {
        ESP_ERROR_CHECK(esp_now_set_pmk(config_.pmk.data()));
    }

    if (config_.enable_long_range) {
        ESP_ERROR_CHECK(
            esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G |
                                                   WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR));
    }

    instance_ = this;

    event_queue_ = xQueueCreate(config.event_queue_size, sizeof(EspNowQueue::Event));
    if (!event_queue_) {
        ESP_LOGE(TAG, "Failed to create event queue");
        esp_now_deinit();
        return false;
    }

    if (xTaskCreate(task_function, "espnow_comm_task", config.task_stack_size, this,
                    config.task_priority, &task_handle_) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task");
        vQueueDelete(event_queue_);
        esp_now_deinit();
        return false;
    }

    initialized_ = true;

    if (persistence_enabled_) {
        loadPeers();
    }

    ESP_LOGI(TAG, "ESP-NOW initialized. Node ID: %u", node_id_);
    return true;
}

void EspNowComm::deinit()
{
    if (!initialized_ || task_handle_ == nullptr) {
        return;
    }

    // Signal the task to terminate
    EspNowQueue::Event event;
    event.type = EspNowQueue::EventType::CMD_TASK_TERMINATE;
    xQueueSend(event_queue_, &event, 0);

    // Wait for the task to confirm termination by setting its handle to NULL
    while (task_handle_ != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Now it's safe to delete the queue
    if (event_queue_ != nullptr) {
        vQueueDelete(event_queue_);
        event_queue_ = nullptr;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);

    peers_.clear();

    esp_now_unregister_recv_cb();
    esp_now_unregister_send_cb();
    esp_now_deinit();

    initialized_ = false;
    instance_    = nullptr;

    xSemaphoreGive(mutex_);

    ESP_LOGI(TAG, "ESP-NOW deinitialized");
}

uint8_t EspNowComm::get_id() const
{
    return node_id_;
}

bool EspNowComm::send(uint8_t node_id,
                      MessageType message_type,
                      const uint8_t *data,
                      size_t length,
                                 bool require_ack)
{
    if (!initialized_ || !event_queue_) {
        return false;
    }
    if (length > EspNowQueue::CommandSendPacket::MAX_PAYLOAD_SIZE) {
        ESP_LOGE(TAG, "Payload size %d exceeds maximum %d", length,
                 EspNowQueue::CommandSendPacket::MAX_PAYLOAD_SIZE);
        return false;
    }

    EspNowQueue::Event event;
    event.type                            = EspNowQueue::EventType::CMD_SEND_PACKET;
    event.data.cmd_send_packet.node_id    = node_id;
    event.data.cmd_send_packet.require_ack = require_ack;
    event.data.cmd_send_packet.message_type = message_type;
    event.data.cmd_send_packet.payload_len = length;
    memcpy(event.data.cmd_send_packet.payload, data, length);

    if (xQueueSend(event_queue_, &event, pdMS_TO_TICKS(100)) != pdPASS) {
        ESP_LOGE(TAG, "Failed to send to event queue");
        return false;
    }
    return true;
}

bool EspNowComm::addPeer(uint8_t node_id,
                         const uint8_t *mac,
                         uint8_t channel,
                         bool encrypt)
{
    if (!initialized_ || !event_queue_) {
        return false;
    }

    EspNowQueue::Event event;
    event.type                         = EspNowQueue::EventType::CMD_ADD_PEER;
    event.data.cmd_add_peer.node_id    = node_id;
    event.data.cmd_add_peer.channel    = channel;
    event.data.cmd_add_peer.encrypt    = encrypt;
    memcpy(event.data.cmd_add_peer.mac_addr, mac, 6);

    if (xQueueSend(event_queue_, &event, pdMS_TO_TICKS(100)) != pdPASS) {
        ESP_LOGE(TAG, "Failed to send CMD_ADD_PEER to event queue");
        return false;
    }
    return true;
}

bool EspNowComm::addPeerInternal(uint8_t node_id,
                                 const uint8_t *mac,
                                 uint8_t channel,
                                 bool encrypt)
{
    for (auto &peer : peers_) {
        if (peer.node_id == node_id || memcmp(peer.mac_address.data(), mac, 6) == 0) {
            return false; // Peer already exists
        }
    }

    if (peers_.size() >= config_.max_peers) {
        return false;
    }

    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, mac, 6);
    peer_info.channel = (channel == 0) ? config_.wifi_channel : channel;
    peer_info.encrypt = encrypt;
    if (encrypt && config_.enable_encryption) {
        memcpy(peer_info.lmk, config_.lmk.data(), 16);
    }

    if (esp_now_add_peer(&peer_info) != ESP_OK) {
        return false;
    }

    PeerInfo new_peer = {};
    new_peer.node_id  = node_id;
    memcpy(new_peer.mac_address.data(), mac, 6);
    new_peer.is_confirmed = true;
    new_peer.is_active    = true;
    new_peer.last_seen    = esp_timer_get_time() / 1000;
    peers_.push_back(new_peer);

    if (on_peer_event_) {
        on_peer_event_(new_peer, true);
        ESP_LOGI(TAG, "Firing on_peer_event callback for node_id: %u", new_peer.node_id);
    }

    if (persistence_enabled_) {
        savePeersToRTCInternal();
        savePeersToNVSInternal();
    }
    ESP_LOGI(TAG, "Peer added: node_id=%u", node_id);
    return true;
}

bool EspNowComm::removePeer(uint8_t node_id)
{
    if (!initialized_ || !event_queue_) {
        return false;
    }
    EspNowQueue::Event event;
    event.type                              = EspNowQueue::EventType::CMD_REMOVE_PEER;
    event.data.cmd_remove_peer_node_id = node_id;

    if (xQueueSend(event_queue_, &event, pdMS_TO_TICKS(100)) != pdPASS) {
        ESP_LOGE(TAG, "Failed to send CMD_REMOVE_PEER to event queue");
        return false;
    }
    return true;
}

const PeerInfo *EspNowComm::getPeerInfo(uint8_t node_id) const
{
    for (const auto &peer : peers_) {
        if (peer.node_id == node_id) {
            return &peer;
        }
    }
    return nullptr;
}

void EspNowComm::task_function(void *instance)
{
    static_cast<EspNowComm *>(instance)->run();
}

void EspNowComm::run()
{
    EspNowQueue::Event event;
    // Main task loop
    while (true) {
        // Wait for an event, with a timeout for periodic tasks
        if (xQueueReceive(event_queue_, &event,
                          pdMS_TO_TICKS(config_.task_process_interval_ms))) {
            // Process the event from the queue
            switch (event.type) {
            case EspNowQueue::EventType::CMD_SEND_PACKET:
                handleSendPacketCommand(event.data.cmd_send_packet);
                break;
            case EspNowQueue::EventType::CMD_ADD_PEER:
                handleAddPeerCommand(event.data.cmd_add_peer);
                break;
            case EspNowQueue::EventType::CMD_REMOVE_PEER:
                handleRemovePeerCommand(event.data.cmd_remove_peer_node_id);
                break;
            case EspNowQueue::EventType::CMD_START_DISCOVERY:
                handleStartDiscoveryCommand(event.data.cmd_start_discovery_timeout_ms);
                break;
            case EspNowQueue::EventType::CMD_STOP_DISCOVERY:
                handleStopDiscoveryCommand();
                break;
            case EspNowQueue::EventType::EVT_PACKET_RECEIVED:
                handlePacketReceivedEvent(event.data.evt_packet_received);
                break;
            case EspNowQueue::EventType::EVT_SEND_STATUS:
                handleSendStatusEvent(event.data.evt_send_status);
                break;
            case EspNowQueue::EventType::CMD_TASK_TERMINATE:
                ESP_LOGI(TAG, "Task terminating...");
                instance_ = nullptr; // Clear static instance
                task_handle_ = nullptr; // Signal that task is cleaning up
                vTaskDelete(NULL);   // Self-delete
                return;              // Should not be reached
            default:
                ESP_LOGW(TAG, "Unknown event type received in task: %d",
                         (int)event.type);
                break;
            }
        } else {
            // Queue receive timed out, perform periodic tasks
            handlePeriodicTasks();
        }
    }
}

void EspNowComm::handlePeriodicTasks()
{
    if (!initialized_)
        return;

    xSemaphoreTake(mutex_, portMAX_DELAY);
    uint32_t now = esp_timer_get_time() / 1000;

    // Check for ACK timeouts
    auto timeouts = ack_manager_.checkTimeouts();
    if (!timeouts.empty() && on_ack_timeout_) {
        for (const auto &event : timeouts) {
            on_ack_timeout_(event.destination_id);
        }
    }

    // Stop discovery if timeout is reached
    if (discovery_active_ && (now > discovery_end_time_)) {
        stopDiscovery();
    }

    // Send periodic pair requests if discovery is active
    static uint32_t last_pair_request = 0;
    if (discovery_active_ && (now - last_pair_request > 2500)) {
        sendPairRequest();
        last_pair_request = now;
    }

    // Send periodic heartbeats
    static uint32_t last_heartbeat = 0;
    if (config_.heartbeat_interval > 0 &&
        (now - last_heartbeat > config_.heartbeat_interval)) {
        sendHeartbeat();
        last_heartbeat = now;
    }

    // Clean up inactive peers
    if (config_.peer_timeout > 0) {
        cleanupInactivePeersInternal();
    }

    xSemaphoreGive(mutex_);
}

void EspNowComm::handleSendPacketCommand(const EspNowQueue::CommandSendPacket &cmd)
{
    xSemaphoreTake(mutex_, portMAX_DELAY);

    // Assemble the packet first, as it's the same for unicast and broadcast
    size_t header_size = 0;
    uint8_t header_buf[sizeof(DataHeader)]; // Max header size
    uint16_t sequence = ack_manager_.getNextSequence();

    if (cmd.message_type == MessageType::DATA) {
        DataHeader header;
        header.version     = 0x01;
        header.type        = MessageType::DATA;
        header.sequence    = sequence;
        header.timestamp   = esp_timer_get_time() / 1000;
        header.source_id   = node_id_;
        header.dest_id     = cmd.node_id;
        header.ttl         = 1;
        header.data_length = cmd.payload_len;
        memcpy(header_buf, &header, sizeof(header));
        header_size = sizeof(header);
    }
    // Note: Other message types like OTA would be handled here
    else {
         // Generic header for other types
        MessageHeader header;
        header.version   = 0x01;
        header.type      = cmd.message_type;
        header.sequence  = sequence;
        header.timestamp = esp_timer_get_time() / 1000;
        header.source_id = node_id_;
        header.dest_id   = cmd.node_id;
        header.ttl       = 1;
        memcpy(header_buf, &header, sizeof(header));
        header_size = sizeof(header);
    }

    uint8_t packet[config_.max_packet_size];
    size_t packet_len = 0;
    memcpy(packet, header_buf, header_size);
    packet_len += header_size;
    memcpy(packet + packet_len, cmd.payload, cmd.payload_len);
    packet_len += cmd.payload_len;
    uint8_t crc        = esp_rom_crc8_le(0, packet, packet_len);
    packet[packet_len] = crc;
    packet_len++;


    if (cmd.node_id == 0xFF) { // Broadcast logic
        xSemaphoreGive(mutex_); // Release mutex before sending

        uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        if (!esp_now_is_peer_exist(broadcast_mac)) {
            esp_now_peer_info_t peer_info = {};
            memcpy(peer_info.peer_addr, broadcast_mac, 6);
            if (esp_now_add_peer(&peer_info) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to add broadcast peer");
                return;
            }
        }

        esp_err_t err = esp_now_send(broadcast_mac, packet, packet_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Broadcast send failed: %s", esp_err_to_name(err));
        }
    } else { // Unicast logic
        PeerInfo *peer = findPeerById(cmd.node_id);
        if (!peer) {
            xSemaphoreGive(mutex_);
            snprintf(last_error_, sizeof(last_error_), "Peer %u not found for sending",
                     cmd.node_id);
            ESP_LOGW(TAG, "%s", last_error_);
            return;
        }

        uint8_t mac_addr[6];
        memcpy(mac_addr, peer->mac_address.data(), 6);

        xSemaphoreGive(mutex_); // Release mutex before sending

        esp_err_t err = esp_now_send(mac_addr, packet, packet_len);
        if (err != ESP_OK) {
            snprintf(last_error_, sizeof(last_error_), "Send failed: %s",
                     esp_err_to_name(err));
            ESP_LOGE(TAG, "%s", last_error_);
        }

        if (cmd.require_ack && config_.ack_timeout > 0) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            // Re-find the peer after re-acquiring the mutex to ensure it's still valid
            PeerInfo *peer_after_send = findPeerById(cmd.node_id);
            if (peer_after_send) {
                ack_manager_.markAsSent(cmd.node_id, sequence);
                peer_after_send->tx_count++;
            }
            xSemaphoreGive(mutex_);
        }
    }
}


void EspNowComm::handleAddPeerCommand(const EspNowQueue::CommandAddPeer &cmd)
{
    xSemaphoreTake(mutex_, portMAX_DELAY);
    addPeerInternal(cmd.node_id, cmd.mac_addr, cmd.channel, cmd.encrypt);
    xSemaphoreGive(mutex_);
}

void EspNowComm::handleRemovePeerCommand(uint8_t node_id)
{
    xSemaphoreTake(mutex_, portMAX_DELAY);
    for (auto it = peers_.begin(); it != peers_.end(); ++it) {
        if (it->node_id == node_id) {
            esp_now_del_peer(it->mac_address.data());
            if (on_peer_event_) {
                on_peer_event_(*it, false);
            }
            peers_.erase(it);
            if (persistence_enabled_) {
                savePeersToRTCInternal();
                savePeersToNVSInternal();
            }
            break; // Exit loop once peer is found and removed
        }
    }
    xSemaphoreGive(mutex_);
}

void EspNowComm::handleStartDiscoveryCommand(uint32_t timeout_ms)
{
    if (discovery_active_) {
        return;
    }
    discovery_active_   = true;
    discovery_end_time_ = (esp_timer_get_time() / 1000) + timeout_ms;
    ESP_LOGI(TAG, "Discovery started for %u ms", timeout_ms);
}

void EspNowComm::handleStopDiscoveryCommand()
{
    if (discovery_active_) {
        discovery_active_ = false;
        ESP_LOGI(TAG, "Discovery stopped");
    }
}

void EspNowComm::handlePacketReceivedEvent(const EspNowQueue::EventPacketReceived &evt)
{
    // This now runs in the context of our task
    handleReceive(&evt.recv_info, evt.data, evt.len);
}

void EspNowComm::handleSendStatusEvent(const EspNowQueue::EventSendStatus &evt)
{
    // This now runs in the context of our task, operating on a safe copy of tx_info
    handleSend(&evt.tx_info, evt.status);
}

void EspNowComm::handleReceive(const esp_now_recv_info_t *recv_info,
                               const uint8_t *data,
                               int len)
{
    if (len < sizeof(MessageHeader) + 1) {
        ESP_LOGW(TAG, "Packet too short for header.");
        return;
    }

    if (esp_rom_crc8_le(0, data, len - 1) != data[len - 1]) {
        ESP_LOGE(TAG, "CRC check failed.");
        return;
    }

    const MessageHeader *header = reinterpret_cast<const MessageHeader *>(data);
    // ESP_LOGI(TAG, "Packet source_id: %u, type: 0x%02X", header->source_id,
    //  (uint8_t)header->type);
    const uint8_t *mac = recv_info->src_addr;

    xSemaphoreTake(mutex_, portMAX_DELAY);

    PeerInfo *peer = findPeerByMac(mac);
    if (peer) {
        peer->last_seen = esp_timer_get_time() / 1000;
        peer->rx_count++;
        peer->is_active = true;
        peer->last_rssi = recv_info->rx_ctrl->rssi;
    }

    switch (header->type) {
    case MessageType::DATA:
        if (header->dest_id == node_id_ || header->dest_id == 0xFF) {
            if (header->dest_id != 0xFF) {
                sendAck(mac, header->sequence);
            }
            if (on_receive_) {
                const uint8_t *payload = data + sizeof(DataHeader);
                size_t payload_len     = len - sizeof(DataHeader) - 1;
                on_receive_(header->source_id, payload, payload_len,
                            recv_info->rx_ctrl->rssi);
            }
        }
        break;
    case MessageType::ACK:
        if (ack_manager_.markAsAcknowledged(header->sequence)) {
            if (on_ack_success_) {
                on_ack_success_(header->source_id);
            }
        }
        break;
    case MessageType::PAIR_REQUEST:
    {
        if (!discovery_active_) {
            break;
        }

        const PairHeader *pair_header = reinterpret_cast<const PairHeader *>(data);
        bool should_pair              = false;

        // Regra: Hub aceita qualquer um.
        if (node_type_ == NodeType::HUB) {
            should_pair = true;
            ESP_LOGI(TAG, "Hub accepting pair request from node type %d",
                     (int)pair_header->node_type);
        }
        // Regra: Não-Hub só aceita Hub.
        else if (pair_header->node_type == NodeType::HUB) {
            should_pair = true;
            ESP_LOGI(TAG, "Device accepting pair request from Hub.");
        }
        else {
            ESP_LOGW(TAG, "Device of type %d rejecting pair request from node type %d",
                     (int)node_type_, (int)pair_header->node_type);
        }

        if (should_pair) {
            addPeerInternal(header->source_id, mac, 0, false);
            sendPairResponse(mac, *pair_header);
        }
        break;
    }
    case MessageType::PAIR_RESPONSE:
        addPeerInternal(header->source_id, mac, 0, false);
        break;

    case MessageType::OTA:
        if (on_ota_command_) {
            const size_t expected_len = sizeof(MessageHeader) + sizeof(OtaCommand) + 1;
            ESP_LOGD(TAG, "OTA message: received len=%d, expected len=%d", len,
                     expected_len);
            if (len == expected_len) {
                const OtaCommand *command =
                    reinterpret_cast<const OtaCommand *>(data + sizeof(MessageHeader));
                on_ota_command_(header->source_id, *command);
            }
            else {
                ESP_LOGW(TAG,
                         "Received OTA command with incorrect size. Got %d, "
                         "expected %d",
                         len, expected_len);
            }
        }
        break;
    default:
        break;
    }

    xSemaphoreGive(mutex_);
}

void EspNowComm::handleSend(const esp_now_send_info_t *tx_info,
                            esp_now_send_status_t status)
{
    xSemaphoreTake(mutex_, portMAX_DELAY);
    PeerInfo *peer = findPeerByMac(tx_info->des_addr);
    if (peer) {
        if (status == ESP_NOW_SEND_SUCCESS) {
            peer->link_quality = std::min(100, peer->link_quality + 1);
        }
        else {
            peer->tx_failures++;
            peer->link_quality = std::max(0, peer->link_quality - 5);
        }
    }
    xSemaphoreGive(mutex_);

    if (on_send_) {
        on_send_(peer ? peer->node_id : 0, status);
    }
}

void EspNowComm::espNowRecvCb(const esp_now_recv_info_t *recv_info,
                              const uint8_t *data,
                              int len)
{
    if (!instance_ || !instance_->event_queue_) {
        return;
    }
    if (len > sizeof(EspNowQueue::EventPacketReceived::data)) {
        return; // Packet too large for our buffer
    }

    EspNowQueue::Event event;
    event.type = EspNowQueue::EventType::EVT_PACKET_RECEIVED;
    memcpy(&event.data.evt_packet_received.recv_info, recv_info,
           sizeof(esp_now_recv_info_t));
    memcpy(event.data.evt_packet_received.data, data, len);
    event.data.evt_packet_received.len = len;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(instance_->event_queue_, &event, &xHigherPriorityTaskWoken);

    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

void EspNowComm::espNowSendCb(const esp_now_send_info_t *tx_info,
                              esp_now_send_status_t status)
{
    if (!instance_ || !instance_->event_queue_ || tx_info == nullptr) {
        return;
    }

    EspNowQueue::Event event;
    event.type = EspNowQueue::EventType::EVT_SEND_STATUS;
    memcpy(&event.data.evt_send_status.tx_info, tx_info, sizeof(esp_now_send_info_t));
    event.data.evt_send_status.status = status;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(instance_->event_queue_, &event, &xHigherPriorityTaskWoken);

    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

PeerInfo *EspNowComm::findPeerByMac(const uint8_t *mac)
{
    for (auto &peer : peers_) {
        if (memcmp(peer.mac_address.data(), mac, 6) == 0)
            return &peer;
    }
    return nullptr;
}

PeerInfo *EspNowComm::findPeerById(uint8_t node_id)
{
    for (auto &peer : peers_) {
        if (peer.node_id == node_id)
            return &peer;
    }
    return nullptr;
}

void EspNowComm::sendAck(const uint8_t *mac, uint16_t sequence)
{
    AckHeader ack = {};
    ack.version   = 0x01;
    ack.type      = MessageType::ACK;
    ack.sequence  = sequence;
    ack.timestamp = esp_timer_get_time() / 1000;
    ack.source_id = node_id_;
    uint8_t packet[sizeof(ack) + 1];
    memcpy(packet, &ack, sizeof(ack));
    packet[sizeof(ack)] = esp_rom_crc8_le(0, packet, sizeof(ack));
    esp_now_send(mac, packet, sizeof(packet));
}

void EspNowComm::sendPairRequest()
{
    uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (!esp_now_is_peer_exist(broadcast_mac)) {
        esp_now_peer_info_t peer_info = {};
        memcpy(peer_info.peer_addr, broadcast_mac, 6);
        esp_now_add_peer(&peer_info);
    }

    PairHeader header = {};
    header.version    = 0x01;
    header.type       = MessageType::PAIR_REQUEST;
    header.sequence   = ack_manager_.getNextSequence();
    header.timestamp  = esp_timer_get_time() / 1000;
    header.source_id  = node_id_;
    header.dest_id    = 0xFF;
    header.node_type  = node_type_;
    snprintf(header.device_name, sizeof(header.device_name), "Device_%u", node_id_);

    uint8_t packet[sizeof(header) + 1];
    memcpy(packet, &header, sizeof(header));
    packet[sizeof(header)] = esp_rom_crc8_le(0, packet, sizeof(header));
    esp_now_send(broadcast_mac, packet, sizeof(packet));
}

void EspNowComm::sendPairResponse(const uint8_t *mac, const PairHeader &req_header)
{
    PairHeader resp_header = {};
    resp_header.version    = 0x01;
    resp_header.type       = MessageType::PAIR_RESPONSE;
    resp_header.sequence   = req_header.sequence;
    resp_header.timestamp  = esp_timer_get_time() / 1000;
    resp_header.source_id  = node_id_;
    resp_header.dest_id    = req_header.source_id;
    resp_header.node_type  = node_type_;
    snprintf(resp_header.device_name, sizeof(resp_header.device_name), "Device_%u",
             node_id_);

    uint8_t packet[sizeof(resp_header) + 1];
    memcpy(packet, &resp_header, sizeof(resp_header));
    packet[sizeof(resp_header)] = esp_rom_crc8_le(0, packet, sizeof(resp_header));
    esp_now_send(mac, packet, sizeof(packet));
}

void EspNowComm::sendHeartbeat()
{
    HeartbeatHeader hb = {};
    hb.version         = 0x01;
    hb.type            = MessageType::HEARTBEAT;
    hb.sequence        = ack_manager_.getNextSequence();
    hb.timestamp       = esp_timer_get_time() / 1000;
    hb.source_id       = node_id_;
    hb.dest_id         = 0xFF; // Broadcast, but sent peer-by-peer
    hb.ttl             = 1;
    hb.battery_level   = 1000; // Placeholder
    hb.status_flags    = 0;
    hb.free_heap       = esp_get_free_heap_size() / 1024;

    uint8_t packet[sizeof(hb) + 1];
    memcpy(packet, &hb, sizeof(hb));
    packet[sizeof(hb)] = esp_rom_crc8_le(0, packet, sizeof(hb));

    for (auto &peer : peers_) {
        if (peer.is_active && peer.is_confirmed) {
            esp_now_send(peer.mac_address.data(), packet, sizeof(packet));
        }
    }
}

void EspNowComm::cleanupInactivePeersInternal()
{
    uint32_t now = esp_timer_get_time() / 1000;
    for (auto it = peers_.begin(); it != peers_.end();) {
        if (!it->is_confirmed && (now - it->last_seen > config_.peer_timeout)) {
            esp_now_del_peer(it->mac_address.data());
            if (on_peer_event_) {
                on_peer_event_(*it, false);
            }
            it = peers_.erase(it);
        }
        else {
            ++it;
        }
    }
}

bool EspNowComm::loadPeers()
{
    if (!persistence_enabled_)
        return false;

    auto rtc_peers = PeerPersistence::loadFromRTC();
    if (!rtc_peers.empty()) {
        ESP_LOGI(TAG, "Loading peers from RTC memory (%zu peers)", rtc_peers.size());
    }
    else {
        auto nvs_peers = PeerPersistence::loadFromNVS();
        if (!nvs_peers.empty()) {
            ESP_LOGI(TAG, "Loading peers from NVS (%zu peers)", nvs_peers.size());
            PeerPersistence::saveToRTC(nvs_peers); // Refresh RTC
            rtc_peers = nvs_peers;
        }
        else {
            ESP_LOGI(TAG, "No persisted peers found.");
            return false;
        }
    }

    for (const auto &p_peer : rtc_peers) {
        addPeer(p_peer.node_id, p_peer.mac, 0, false);
    }
    return true;
}

std::vector<PeerPersistence::PersistentPeer> EspNowComm::getPeers() const
{
    xSemaphoreTake(mutex_, portMAX_DELAY);
    auto peers = getPeersInternal();
    xSemaphoreGive(mutex_);
    return peers;
}

std::vector<PeerPersistence::PersistentPeer> EspNowComm::getPeersInternal() const
{
    std::vector<PeerPersistence::PersistentPeer> p_peers;
    for (const auto &peer : peers_) {
        if (peer.is_confirmed) {
            PeerPersistence::PersistentPeer p;
            memcpy(p.mac, peer.mac_address.data(), 6);
            p.node_id = peer.node_id;
            p_peers.push_back(p);
        }
    }
    return p_peers;
}

bool EspNowComm::savePeersToNVS()
{
    xSemaphoreTake(mutex_, portMAX_DELAY);
    bool success = savePeersToNVSInternal();
    xSemaphoreGive(mutex_);
    return success;
}

bool EspNowComm::savePeersToNVSInternal()
{
    if (!persistence_enabled_)
        return false;
    auto p_peers = getPeersInternal();
    if (p_peers.empty())
        return true;
    return PeerPersistence::saveToNVS(p_peers);
}

bool EspNowComm::savePeersToRTC()
{
    xSemaphoreTake(mutex_, portMAX_DELAY);
    bool success = savePeersToRTCInternal();
    xSemaphoreGive(mutex_);
    return success;
}

bool EspNowComm::savePeersToRTCInternal()
{
    if (!persistence_enabled_)
        return false;
    auto p_peers = getPeersInternal();
    if (p_peers.empty())
        return true;
    return PeerPersistence::saveToRTC(p_peers);
}

void EspNowComm::setReceiveCallback(OnReceiveCallback callback)
{
    on_receive_ = callback;
}

void EspNowComm::setSendCallback(OnSendCallback callback)
{
    on_send_ = callback;
}

void EspNowComm::setPeerEventCallback(OnPeerEventCallback callback)
{
    on_peer_event_ = callback;
}

void EspNowComm::setAckSuccessCallback(OnAckSuccessCallback callback)
{
    on_ack_success_ = callback;
}

void EspNowComm::setAckTimeoutCallback(OnAckTimeoutCallback callback)
{
    on_ack_timeout_ = callback;
}

void EspNowComm::setOtaCommandCallback(OnOtaCommandCallback callback)
{
    on_ota_command_ = callback;
}

size_t EspNowComm::getPeerCount() const
{
    return peers_.size();
}

const char *EspNowComm::getLastError() const
{
    return last_error_;
}

bool EspNowComm::startDiscovery(uint32_t timeout_ms)
{
    if (!initialized_ || !event_queue_) {
        return false;
    }
    EspNowQueue::Event event;
    event.type                                  = EspNowQueue::EventType::CMD_START_DISCOVERY;
    event.data.cmd_start_discovery_timeout_ms = timeout_ms;
    if (xQueueSend(event_queue_, &event, pdMS_TO_TICKS(100)) != pdPASS) {
        ESP_LOGE(TAG, "Failed to send CMD_START_DISCOVERY to event queue");
        return false;
    }
    return true;
}

bool EspNowComm::stopDiscovery()
{
    if (!initialized_ || !event_queue_) {
        return false;
    }
    EspNowQueue::Event event;
    event.type = EspNowQueue::EventType::CMD_STOP_DISCOVERY;
    if (xQueueSend(event_queue_, &event, pdMS_TO_TICKS(100)) != pdPASS) {
        ESP_LOGE(TAG, "Failed to send CMD_STOP_DISCOVERY to event queue");
        return false;
    }
    return true;
}
