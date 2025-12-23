// espnow_comm.cpp
#include "espnow_comm.hpp"
#include "esp_mac.h"
#include "esp_wifi.h" // For esp_wifi_set_protocol
#include "esp_rom_crc.h"
#include "freertos/FreeRTOS.h"
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
{
    memset(last_error_, 0, sizeof(last_error_));

    if (persistence_enabled_) {
        PeerPersistence::initNVS();
    }

    // Generate a default node_id based on the MAC address (last byte).
    // This can be overridden by the application if needed.
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    node_id_ = mac[5];

    // Create the mutex for thread safety.
    mutex_ = xSemaphoreCreateMutex();
}

EspNowComm::~EspNowComm()
{
    deinit();
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

    config_ = config;

    // Initialize WiFi in Station mode
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Initialize ESP-NOW
    ESP_ERROR_CHECK(esp_now_init());

    // Register callbacks
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espNowRecvCb));
    ESP_ERROR_CHECK(esp_now_register_send_cb(espNowSendCb));

    // Set PMK if encryption is enabled
    if (config_.enable_encryption) {
        ESP_ERROR_CHECK(esp_now_set_pmk(config_.pmk.data()));
    }

    // Set long-range mode if enabled
    if (config_.enable_long_range) {
        ESP_ERROR_CHECK(
            esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G |
                                                   WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR));
    }

    instance_    = this;
    initialized_ = true;

    // Load persisted peers if enabled
    if (persistence_enabled_) {
        loadPeersIntelligently();
    }

    ESP_LOGI(TAG, "ESP-NOW initialized. Node ID: %u", node_id_);
    return true;
}

void EspNowComm::deinit()
{
    if (!initialized_) return;

    xSemaphoreTake(mutex_, portMAX_DELAY);

    stopDiscovery();
    peers_.clear();

    esp_now_unregister_recv_cb();
    esp_now_unregister_send_cb();
    esp_now_deinit();

    esp_wifi_stop();
    esp_wifi_deinit();

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
                      const uint8_t *data,
                      size_t length,
                      bool require_ack)
{
    if (!initialized_) {
        strncpy(last_error_, "Not initialized", sizeof(last_error_) - 1);
        return false;
    }

    if (length > config_.max_packet_size - sizeof(DataHeader) - 1) { // 1 byte for CRC
        snprintf(last_error_, sizeof(last_error_), "Data too large: %zu > %u", length,
                 config_.max_packet_size - sizeof(DataHeader) - 1);
        return false;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);

    PeerInfo *peer = findPeerById(node_id);
    if (!peer) {
        xSemaphoreGive(mutex_);
        snprintf(last_error_, sizeof(last_error_), "Peer %u not found", node_id);
        return false;
    }

    // Prepare message header
    DataHeader header;
    header.version       = 0x01;
    header.type          = MessageType::DATA;
    header.sequence      = ack_manager_.getNextSequence();
    header.timestamp     = esp_timer_get_time() / 1000; // ms
    header.source_id     = node_id_;
    header.dest_id       = node_id;
    header.ttl           = 1;
    header.data_length   = length;
    header.data_type     = 0;
    header.fragmentation = 0;

    // Assemble packet: header + data + crc
    uint8_t packet[config_.max_packet_size];
    size_t packet_len = 0;

    memcpy(packet, &header, sizeof(header));
    packet_len += sizeof(header);

    memcpy(packet + packet_len, data, length);
    packet_len += length;

    // Calculate CRC8 over the header and data
    uint8_t crc = esp_rom_crc8_le(0, packet, packet_len);
    packet[packet_len++] = crc;

    xSemaphoreGive(mutex_);

    // Send via ESP-NOW
    esp_err_t err = esp_now_send(peer->mac_address.data(), packet, packet_len);

    if (err != ESP_OK) {
        snprintf(last_error_, sizeof(last_error_), "Send failed: %s", esp_err_to_name(err));
        return false;
    }

    // Track for acknowledgment if required
    if (require_ack && config_.ack_timeout > 0) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        ack_manager_.markAsSent(node_id, header.sequence);
        peer->tx_count++;
        xSemaphoreGive(mutex_);
    }

    return true;
}

bool EspNowComm::broadcast(const uint8_t *data, size_t length)
{
    if (!config_.allow_broadcast) {
        strncpy(last_error_, "Broadcast not allowed", sizeof(last_error_) - 1);
        return false;
    }

    const size_t max_payload = config_.max_packet_size - sizeof(DataHeader) - 1;
    if (length > max_payload) {
        snprintf(last_error_, sizeof(last_error_), "Packet too large for broadcast: %zu > %zu", length, max_payload);
        return false;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);

    // Prepare header
    DataHeader header;
    header.version       = 0x01;
    header.type          = MessageType::DATA;
    header.sequence      = ack_manager_.getNextSequence();
    header.timestamp     = esp_timer_get_time() / 1000;
    header.source_id     = node_id_;
    header.dest_id       = 0xFF; // Broadcast ID
    header.ttl           = 3;
    header.data_length   = length;
    header.data_type     = 0;
    header.fragmentation = 0;

    // Assemble packet
    uint8_t packet[config_.max_packet_size];
    size_t packet_len = 0;
    memcpy(packet, &header, sizeof(header));
    packet_len += sizeof(header);

    memcpy(packet + packet_len, data, length);
    packet_len += length;

    // Calculate CRC
    uint8_t crc = esp_rom_crc8_le(0, packet, packet_len);
    packet[packet_len++] = crc;

    xSemaphoreGive(mutex_);

    uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_err_t err = esp_now_send(broadcast_mac, packet, packet_len);

    if (err != ESP_OK) {
        snprintf(last_error_, sizeof(last_error_), "Broadcast failed: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}

bool EspNowComm::addPeer(uint8_t node_id, const uint8_t *mac, uint8_t channel, bool encrypt)
{
    xSemaphoreTake(mutex_, portMAX_DELAY);

    // Check for duplicates
    for (auto &peer : peers_) {
        if (peer.node_id == node_id) {
            snprintf(last_error_, sizeof(last_error_), "Peer with node_id %u already exists", node_id);
            xSemaphoreGive(mutex_);
            return false;
        }
        if (memcmp(peer.mac_address.data(), mac, 6) == 0) {
            strncpy(last_error_, "Peer with this MAC already exists", sizeof(last_error_) - 1);
            xSemaphoreGive(mutex_);
            return false;
        }
    }

    // Check limit
    if (peers_.size() >= config_.max_peers) {
        snprintf(last_error_, sizeof(last_error_), "Maximum peers reached (%u)", config_.max_peers);
        xSemaphoreGive(mutex_);
        return false;
    }

    // Add peer to ESP-NOW's internal list
    esp_now_peer_info_t peer_info;
    memset(&peer_info, 0, sizeof(peer_info));
    memcpy(peer_info.peer_addr, mac, 6);
    peer_info.channel = (channel == 0) ? config_.wifi_channel : channel;
    peer_info.encrypt = encrypt;
    if (encrypt && config_.enable_encryption) {
        memcpy(peer_info.lmk, config_.lmk.data(), 16);
    }

    esp_err_t err = esp_now_add_peer(&peer_info);
    if (err != ESP_OK) {
        snprintf(last_error_, sizeof(last_error_), "Failed to add peer to ESP-NOW: %s", esp_err_to_name(err));
        xSemaphoreGive(mutex_);
        return false;
    }

    // Add to our internal list
    PeerInfo new_peer = {}; // Zero-initialize
    new_peer.node_id = node_id;
    memcpy(new_peer.mac_address.data(), mac, 6);
    new_peer.alias = "peer_" + std::to_string(node_id);
    new_peer.first_seen = esp_timer_get_time() / 1000;
    new_peer.last_seen = new_peer.first_seen;
    new_peer.link_quality = 100;
    new_peer.is_confirmed = true;
    new_peer.is_encrypted = encrypt;
    new_peer.is_active = true;
    new_peer.preferred_channel = peer_info.channel;
    new_peer.heartbeat_interval = config_.heartbeat_interval;
    new_peer.last_heartbeat = new_peer.first_seen;

    peers_.push_back(new_peer);

    // Notify application via callback
    if (on_peer_event_) {
        on_peer_event_(new_peer, true);
    }

    xSemaphoreGive(mutex_);

    // Persist changes if enabled
    if (persistence_enabled_) {
        savePeersToRTC();
        savePeersToNVS();
    }

    ESP_LOGI(TAG, "Peer added: node_id=%u, MAC=%02X:%02X:%02X:%02X:%02X:%02X", node_id,
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return true;
}

bool EspNowComm::removePeer(uint8_t node_id)
{
    xSemaphoreTake(mutex_, portMAX_DELAY);

    for (auto it = peers_.begin(); it != peers_.end(); ++it) {
        if (it->node_id == node_id) {
            esp_now_del_peer(it->mac_address.data());

            if (on_peer_event_) {
                on_peer_event_(*it, false);
            }

            ESP_LOGI(TAG, "Peer removed: node_id=%u", node_id);
            peers_.erase(it);

            xSemaphoreGive(mutex_);

            if (persistence_enabled_) {
                savePeersToRTC();
                savePeersToNVS();
            }
            return true;
        }
    }

    xSemaphoreGive(mutex_);
    snprintf(last_error_, sizeof(last_error_), "Peer %u not found", node_id);
    return false;
}

const PeerInfo *EspNowComm::getPeerInfo(uint8_t node_id) const
{
    // This const method cannot take the mutex. If thread safety is critical here,
    // the method should return a copy, not a pointer.
    // For now, we assume this is for infrequent, non-critical access.
    for (const auto &peer : peers_) {
        if (peer.node_id == node_id) {
            return &peer;
        }
    }
    return nullptr;
}


void EspNowComm::process()
{
    if (!initialized_) return;

    xSemaphoreTake(mutex_, portMAX_DELAY);

    // 1. Process ACK timeouts
    auto timeouts = ack_manager_.checkTimeouts();
    if (!timeouts.empty()) {
        // Handle timeouts, e.g., notify application
        for (auto seq : timeouts) {
            ESP_LOGW(TAG, "ACK timeout for sequence %u", seq);
        }
    }

    // 2. Stop discovery if it has expired
    if (discovery_active_ && (esp_timer_get_time() / 1000 > discovery_end_time_)) {
        stopDiscovery();
    }

    // 3. Send heartbeats periodically
    static uint32_t last_heartbeat = 0;
    uint32_t now = esp_timer_get_time() / 1000;
    if (config_.heartbeat_interval > 0 && (now - last_heartbeat > config_.heartbeat_interval)) {
        sendHeartbeat();
        last_heartbeat = now;
    }

    // 4. Clean up inactive peers
    if (config_.peer_timeout > 0) {
        cleanupInactivePeers();
    }

    xSemaphoreGive(mutex_);
}


// Internal handler methods
void EspNowComm::handleReceive(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    const size_t min_len = sizeof(MessageHeader) + 1; // 1 for CRC
    if (len < min_len) {
        ESP_LOGW(TAG, "Packet too short: %d bytes", len);
        return;
    }

    // Validate CRC
    const uint8_t *payload_start = data;
    size_t payload_len = len - 1;
    uint8_t received_crc = data[payload_len];
    uint8_t calculated_crc = esp_rom_crc8_le(0, payload_start, payload_len);

    if (received_crc != calculated_crc) {
        ESP_LOGW(TAG, "Invalid CRC: got %02X, expected %02X", received_crc, calculated_crc);
        return;
    }

    const MessageHeader *header = reinterpret_cast<const MessageHeader *>(data);
    const uint8_t *mac = recv_info->src_addr;

    xSemaphoreTake(mutex_, portMAX_DELAY);

    PeerInfo *peer = findPeerByMac(mac);
    if (!peer && config_.auto_pairing && header->type != MessageType::PAIR_REQUEST) {
        if (peers_.size() < config_.max_peers) {
            PeerInfo new_peer = {};
            memcpy(new_peer.mac_address.data(), mac, 6);
            new_peer.node_id = header->source_id;
            new_peer.alias = "auto_peer";
            new_peer.first_seen = esp_timer_get_time() / 1000;
            new_peer.last_seen = new_peer.first_seen;
            new_peer.rx_count = 1;
            new_peer.is_confirmed = false;
            new_peer.is_active = true;
            peers_.push_back(new_peer);
            peer = &peers_.back();
            ESP_LOGI(TAG, "Auto-paired with node %u", header->source_id);
        }
    }

    if (peer) {
        peer->last_seen = esp_timer_get_time() / 1000;
        peer->rx_count++;
        peer->is_active = true;
        peer->last_rssi = recv_info->rx_ctrl->rssi;
    }

    // Process message based on type
    switch (header->type) {
    case MessageType::DATA:
        if (header->dest_id == node_id_ || header->dest_id == 0xFF) {
            if (header->dest_id != 0xFF) {
                sendAck(mac, header->sequence);
            }
            if (on_receive_) {
                const uint8_t *app_payload = data + sizeof(MessageHeader);
                size_t app_payload_len = payload_len - sizeof(MessageHeader);
                on_receive_(header->source_id, app_payload, app_payload_len, recv_info->rx_ctrl->rssi);
            }
        }
        break;

    case MessageType::ACK:
        if (ack_manager_.markAsAcknowledged(header->sequence)) {
            ESP_LOGD(TAG, "ACK received for sequence %u", header->sequence);
        }
        break;

    case MessageType::HEARTBEAT:
        if (peer) {
            peer->last_heartbeat = esp_timer_get_time() / 1000;
        }
        break;

    default:
        ESP_LOGW(TAG, "Unknown message type: %u", (uint8_t)header->type);
        break;
    }

    xSemaphoreGive(mutex_);
}

void EspNowComm::handleSend(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    const uint8_t* mac = tx_info->des_addr;

    xSemaphoreTake(mutex_, portMAX_DELAY);

    PeerInfo *peer = findPeerByMac(mac);
    uint8_t node_id = peer ? peer->node_id : 0;

    if (peer) {
        if (status == ESP_NOW_SEND_SUCCESS) {
            peer->link_quality = std::min(100, peer->link_quality + 1);
        } else {
            peer->tx_failures++;
            peer->link_quality = std::max(0, peer->link_quality - 5);
        }
    }

    xSemaphoreGive(mutex_);

    if (on_send_) {
        on_send_(node_id, status);
    }
}


// Static callbacks that delegate to the class instance
void EspNowComm::espNowRecvCb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    if (instance_ && recv_info && data && len > 0) {
        instance_->handleReceive(recv_info, data, len);
    }
}

void EspNowComm::espNowSendCb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    if (instance_ && tx_info) {
        instance_->handleSend(tx_info, status);
    }
}


// Private helper methods
PeerInfo *EspNowComm::findPeerByMac(const uint8_t *mac)
{
    for (auto &peer : peers_) {
        if (memcmp(peer.mac_address.data(), mac, 6) == 0) {
            return &peer;
        }
    }
    return nullptr;
}

PeerInfo *EspNowComm::findPeerById(uint8_t node_id)
{
    for (auto &peer : peers_) {
        if (peer.node_id == node_id) {
            return &peer;
        }
    }
    return nullptr;
}

void EspNowComm::sendAck(const uint8_t *mac, uint16_t sequence)
{
    AckHeader ack = {};
    ack.version = 0x01;
    ack.type = MessageType::ACK;
    ack.sequence = sequence; // Acknowledge the received sequence
    ack.timestamp = esp_timer_get_time() / 1000;
    ack.source_id = node_id_;

    uint8_t packet[sizeof(ack) + 1]; // 1 for CRC
    memcpy(packet, &ack, sizeof(ack));
    packet[sizeof(ack)] = esp_rom_crc8_le(0, packet, sizeof(ack));

    esp_now_send(mac, packet, sizeof(packet));
}

void EspNowComm::sendHeartbeat()
{
    HeartbeatHeader hb = {};
    hb.version       = 0x01;
    hb.type          = MessageType::HEARTBEAT;
    hb.sequence      = ack_manager_.getNextSequence();
    hb.timestamp     = esp_timer_get_time() / 1000;
    hb.source_id     = node_id_;
    hb.dest_id       = 0xFF; // Broadcast, but sent peer-by-peer
    hb.ttl           = 1;
    hb.battery_level = 1000; // Placeholder for 100.0%
    hb.status_flags  = 0;
    hb.free_heap     = esp_get_free_heap_size() / 1024; // Free heap in KB

    uint8_t packet[sizeof(hb) + 1];
    memcpy(packet, &hb, sizeof(hb));
    packet[sizeof(hb)] = esp_rom_crc8_le(0, packet, sizeof(hb));

    // This method is called from process(), which already holds the mutex.
    for (auto &peer : peers_) {
        if (peer.is_active && peer.is_confirmed) {
            esp_now_send(peer.mac_address.data(), packet, sizeof(packet));
        }
    }
}

void EspNowComm::cleanupInactivePeers()
{
    uint32_t now = esp_timer_get_time() / 1000;

    // This method is called from process(), which already holds the mutex.
    for (auto it = peers_.begin(); it != peers_.end();) {
        // Only remove peers that were auto-paired and haven't been seen recently.
        if (!it->is_confirmed && (now - it->last_seen > config_.peer_timeout)) {
            ESP_LOGI(TAG, "Removing inactive unconfirmed peer: node_id=%u", it->node_id);

            if (on_peer_event_) {
                on_peer_event_(*it, false);
            }

            esp_now_del_peer(it->mac_address.data());
            it = peers_.erase(it);
        }
        else {
            ++it;
        }
    }
}

bool EspNowComm::loadPeersIntelligently()
{
    if (!persistence_enabled_) return false;

    // Try RTC first
    auto rtc_peers = PeerPersistence::loadFromRTC();
    if (!rtc_peers.empty()) {
        ESP_LOGI(TAG, "Loading peers from RTC memory (%zu peers)", rtc_peers.size());
    } else {
        // Fallback to NVS
        auto nvs_peers = PeerPersistence::loadFromNVS();
        if (!nvs_peers.empty()) {
            ESP_LOGI(TAG, "Loading peers from NVS (%zu peers)", nvs_peers.size());
            PeerPersistence::saveToRTC(nvs_peers); // Refresh RTC
            rtc_peers = nvs_peers;
        } else {
            ESP_LOGI(TAG, "No persisted peers found.");
            return false;
        }
    }

    // Add loaded peers to the system
    for (const auto &p_peer : rtc_peers) {
        addPeer(p_peer.node_id, p_peer.mac, 0, false);
    }
    return true;
}

std::vector<PeerPersistence::PersistentPeer> EspNowComm::getPersistentPeers() const
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
    if (!persistence_enabled_ || !initialized_) return false;

    xSemaphoreTake(mutex_, portMAX_DELAY);
    auto p_peers = getPersistentPeers();
    xSemaphoreGive(mutex_);

    if(p_peers.empty()) return true;

    bool success = PeerPersistence::saveToNVS(p_peers);
    if (success) {
        ESP_LOGI(TAG, "Saved %zu peers to NVS", p_peers.size());
    }
    return success;
}

bool EspNowComm::savePeersToRTC()
{
    if (!persistence_enabled_ || !initialized_) return false;

    xSemaphoreTake(mutex_, portMAX_DELAY);
    auto p_peers = getPersistentPeers();
    xSemaphoreGive(mutex_);

    if(p_peers.empty()) return true;

    bool success = PeerPersistence::saveToRTC(p_peers);
    if (success) {
        ESP_LOGD(TAG, "Saved %zu peers to RTC", p_peers.size());
    }
    return success;
}


// Setters for callbacks
void EspNowComm::setReceiveCallback(OnReceiveCallback callback) { on_receive_ = callback; }
void EspNowComm::setSendCallback(OnSendCallback callback) { on_send_ = callback; }
void EspNowComm::setPeerEventCallback(OnPeerEventCallback callback) { on_peer_event_ = callback; }

size_t EspNowComm::getPeerCount() const { return peers_.size(); }
const char *EspNowComm::getLastError() const { return last_error_; }

// Start/Stop Discovery
bool EspNowComm::startDiscovery(uint32_t timeout_ms) {
    if (discovery_active_) {
        strncpy(last_error_, "Discovery already active", sizeof(last_error_) - 1);
        return false;
    }
    if (!config_.enable_discovery) {
        strncpy(last_error_, "Discovery not enabled in config", sizeof(last_error_) - 1);
        return false;
    }
    discovery_active_ = true;
    discovery_end_time_ = (esp_timer_get_time() / 1000) + timeout_ms;
    ESP_LOGI(TAG, "Discovery started for %u ms", timeout_ms);
    return true;
}

void EspNowComm::stopDiscovery() {
    if (!discovery_active_) return;
    discovery_active_ = false;
    ESP_LOGI(TAG, "Discovery stopped");
}