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
{
    memset(last_error_, 0, sizeof(last_error_));

    if (persistence_enabled_) {
        PeerPersistence::initNVS();
    }

    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    node_id_ = mac[3] ^ mac[4] ^ mac[5];

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

    instance_    = this;
    initialized_ = true;

    if (persistence_enabled_) {
        loadPeers();
    }

    ESP_LOGI(TAG, "ESP-NOW initialized. Node ID: %u", node_id_);
    return true;
}

void EspNowComm::deinit()
{
    if (!initialized_)
        return;

    xSemaphoreTake(mutex_, portMAX_DELAY);

    stopDiscovery();
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
                      const uint8_t *data,
                      size_t length,
                      bool require_ack)
{
    if (!initialized_) {
        strncpy(last_error_, "Not initialized", sizeof(last_error_) - 1);
        return false;
    }

    if (length > config_.max_packet_size - sizeof(DataHeader) - 1) {
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

    DataHeader header;
    header.version     = 0x01;
    header.type        = MessageType::DATA;
    header.sequence    = ack_manager_.getNextSequence();
    header.timestamp   = esp_timer_get_time() / 1000;
    header.source_id   = node_id_;
    header.dest_id     = node_id;
    header.ttl         = 1;
    header.data_length = length;

    uint8_t packet[config_.max_packet_size];
    size_t packet_len = 0;
    memcpy(packet, &header, sizeof(header));
    packet_len += sizeof(header);
    memcpy(packet + packet_len, data, length);
    packet_len += length;
    uint8_t crc        = esp_rom_crc8_le(0, packet, packet_len);
    packet[packet_len] = crc;
    packet_len++;

    xSemaphoreGive(mutex_);

    esp_err_t err = esp_now_send(peer->mac_address.data(), packet, packet_len);
    if (err != ESP_OK) {
        snprintf(last_error_, sizeof(last_error_), "Send failed: %s",
                 esp_err_to_name(err));
        return false;
    }

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
    const size_t max_payload = config_.max_packet_size - sizeof(DataHeader) - 1;
    if (length > max_payload) {
        snprintf(last_error_, sizeof(last_error_),
                 "Packet too large for broadcast: %zu > %zu", length, max_payload);
        return false;
    }

    uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (!esp_now_is_peer_exist(broadcast_mac)) {
        esp_now_peer_info_t peer_info = {};
        memcpy(peer_info.peer_addr, broadcast_mac, 6);
        if (esp_now_add_peer(&peer_info) != ESP_OK) {
            strncpy(last_error_, "Failed to add broadcast peer", sizeof(last_error_) - 1);
            return false;
        }
    }

    DataHeader header;
    header.version     = 0x01;
    header.type        = MessageType::DATA;
    header.sequence    = ack_manager_.getNextSequence();
    header.timestamp   = esp_timer_get_time() / 1000;
    header.source_id   = node_id_;
    header.dest_id     = 0xFF;
    header.ttl         = 3;
    header.data_length = length;

    uint8_t packet[config_.max_packet_size];
    size_t packet_len = 0;
    memcpy(packet, &header, sizeof(header));
    packet_len += sizeof(header);
    memcpy(packet + packet_len, data, length);
    packet_len += length;
    uint8_t crc        = esp_rom_crc8_le(0, packet, packet_len);
    packet[packet_len] = crc;
    packet_len++;

    if (esp_now_send(broadcast_mac, packet, packet_len) != ESP_OK) {
        strncpy(last_error_, "Broadcast send failed", sizeof(last_error_) - 1);
        return false;
    }
    return true;
}

bool EspNowComm::addPeer(uint8_t node_id,
                         const uint8_t *mac,
                         uint8_t channel,
                         bool encrypt)
{
    xSemaphoreTake(mutex_, portMAX_DELAY);
    bool result = addPeerInternal(node_id, mac, channel, encrypt);
    xSemaphoreGive(mutex_);
    return result;
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
    xSemaphoreTake(mutex_, portMAX_DELAY);
    for (auto it = peers_.begin(); it != peers_.end(); ++it) {
        if (it->node_id == node_id) {
            esp_now_del_peer(it->mac_address.data());
            if (on_peer_event_) {
                on_peer_event_(*it, false);
            }
            peers_.erase(it);
            xSemaphoreGive(mutex_);
            if (persistence_enabled_) {
                savePeersToRTCInternal();
                savePeersToNVSInternal();
            }
            return true;
        }
    }
    xSemaphoreGive(mutex_);
    return false;
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

void EspNowComm::process()
{
    if (!initialized_)
        return;

    xSemaphoreTake(mutex_, portMAX_DELAY);
    uint32_t now = esp_timer_get_time() / 1000;

    auto timeouts = ack_manager_.checkTimeouts();
    if (!timeouts.empty() && on_ack_timeout_) {
        for (const auto &event : timeouts) {
            on_ack_timeout_(event.destination_id);
        }
    }

    if (discovery_active_ && (now > discovery_end_time_)) {
        stopDiscovery();
    }

    static uint32_t last_pair_request = 0;
    if (discovery_active_ && (now - last_pair_request > 2500)) {
        sendPairRequest();
        last_pair_request = now;
    }

    static uint32_t last_heartbeat = 0;
    if (config_.heartbeat_interval > 0 &&
        (now - last_heartbeat > config_.heartbeat_interval)) {
        sendHeartbeat();
        last_heartbeat = now;
    }

    if (config_.peer_timeout > 0) {
        cleanupInactivePeersInternal();
    }

    xSemaphoreGive(mutex_);
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
        if (discovery_active_) {
            addPeerInternal(header->source_id, mac, 0, false);
            sendPairResponse(mac, *reinterpret_cast<const PairHeader *>(data));
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
    if (instance_)
        instance_->handleReceive(recv_info, data, len);
}

void EspNowComm::espNowSendCb(const esp_now_send_info_t *tx_info,
                              esp_now_send_status_t status)
{
    if (instance_)
        instance_->handleSend(tx_info, status);
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

void EspNowComm::cleanupInactivePeers()
{
    xSemaphoreTake(mutex_, portMAX_DELAY);
    cleanupInactivePeersInternal();
    xSemaphoreGive(mutex_);
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
    if (discovery_active_) {
        return false;
    }
    discovery_active_   = true;
    discovery_end_time_ = (esp_timer_get_time() / 1000) + timeout_ms;
    ESP_LOGI(TAG, "Discovery started for %u ms", timeout_ms);
    return true;
}

bool EspNowComm::sendOtaCommand(uint8_t node_id, const OtaCommand &command)
{
    if (!initialized_) {
        strncpy(last_error_, "Not initialized", sizeof(last_error_) - 1);
        return false;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);

    PeerInfo *peer = findPeerById(node_id);
    if (!peer) {
        xSemaphoreGive(mutex_);
        snprintf(last_error_, sizeof(last_error_), "Peer %u not found", node_id);
        return false;
    }

    MessageHeader header;
    header.version   = 0x01;
    header.type      = MessageType::OTA;
    header.sequence  = ack_manager_.getNextSequence();
    header.timestamp = esp_timer_get_time() / 1000;
    header.source_id = node_id_;
    header.dest_id   = node_id;
    header.ttl       = 1;

    uint8_t packet[sizeof(MessageHeader) + sizeof(OtaCommand) + 1];
    size_t packet_len = 0;
    memcpy(packet, &header, sizeof(header));
    packet_len += sizeof(header);
    memcpy(packet + packet_len, &command, sizeof(command));
    packet_len += sizeof(command);
    uint8_t crc        = esp_rom_crc8_le(0, packet, packet_len);
    packet[packet_len] = crc;
    packet_len++;

    xSemaphoreGive(mutex_);

    esp_err_t err = esp_now_send(peer->mac_address.data(), packet, packet_len);
    if (err != ESP_OK) {
        snprintf(last_error_, sizeof(last_error_), "OTA command send failed: %s",
                 esp_err_to_name(err));
        return false;
    }

    return true;
}

void EspNowComm::stopDiscovery()
{
    if (discovery_active_) {
        discovery_active_ = false;
        ESP_LOGI(TAG, "Discovery stopped");
    }
}
