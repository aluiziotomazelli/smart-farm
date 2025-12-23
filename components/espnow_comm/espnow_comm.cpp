// espnow_comm.cpp
#include "espnow_comm.hpp"
#include "acknowledgment_manager.hpp"
#include "esp_mac.h"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "EspNowComm";

// Inicializar instância estática
EspNowComm *EspNowComm::instance_ = nullptr;

EspNowComm::EspNowComm(bool enable_persistence)
    : node_id_(0)
    , initialized_(false)
    , persistence_enabled_(enable_persistence)
    , ack_manager_(nullptr)
    , discovery_active_(false)
    , discovery_end_time_(0)
{
    memset(last_error_, 0, sizeof(last_error_));

    if (persistence_enabled_) {
        PeerPersistence::initNVS();
    }

    // Gerar node_id baseado no MAC (último byte)
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    node_id_ = mac[5];

    ack_manager_ = new AcknowledgmentManager();
}

EspNowComm::~EspNowComm()
{
    deinit();
    delete ack_manager_;
}

bool EspNowComm::init(const ESPNOWConfig &config)
{
    if (initialized_) {
        strncpy(last_error_, "Already initialized", sizeof(last_error_) - 1);
        return false;
    }

    config_ = config;

    // Inicializar WiFi em modo STA
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Inicializar ESP-NOW
    ESP_ERROR_CHECK(esp_now_init());

    // Registrar callbacks

    ESP_ERROR_CHECK(esp_now_register_recv_cb(espNowRecvCb));
    ESP_ERROR_CHECK(esp_now_register_send_cb(espNowSendCb));

    // Configurar PMK se criptografia ativada
    if (config_.enable_encryption) {
        ESP_ERROR_CHECK(esp_now_set_pmk(config_.pmk.data()));
    }

    // Configurar long-range se necessário
    if (config_.enable_long_range) {
        ESP_ERROR_CHECK(
            esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G |
                                                   WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR));
    }

    instance_    = this;
    initialized_ = true;
    // Load peers intelligently if persistence enabled
    if (persistence_enabled_) {
        loadPeersIntelligently();
    }

    ESP_LOGI(TAG, "ESP-NOW initialized. Node ID: %u", node_id_);
    return true;
}

void EspNowComm::deinit()
{
    if (!initialized_)
        return;

    // Parar descoberta
    stopDiscovery();

    // Remover todos os peers
    peers_.clear();

    // Desregistrar callbacks
    esp_now_unregister_recv_cb();
    esp_now_unregister_send_cb();

    // Deinicializar ESP-NOW
    esp_now_deinit();

    // Parar WiFi
    esp_wifi_stop();
    esp_wifi_deinit();

    initialized_ = false;
    instance_    = nullptr;

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

    if (length > config_.max_packet_size - sizeof(DataHeader)) {
        snprintf(last_error_, sizeof(last_error_), "Data too large: %zu > %u", length,
                 config_.max_packet_size - sizeof(DataHeader));
        return false;
    }

    PeerInfo *peer = findPeerById(node_id);
    if (!peer) {
        snprintf(last_error_, sizeof(last_error_), "Peer %u not found", node_id);
        return false;
    }

    // Preparar cabeçalho da mensagem
    DataHeader header;
    header.version       = 0x01;
    header.type          = MessageType::DATA;
    header.sequence      = ack_manager_->getNextSequence();
    header.timestamp     = esp_timer_get_time() / 1000; // ms
    header.source_id     = node_id_;
    header.dest_id       = node_id;
    header.ttl           = 1;
    header.data_length   = length;
    header.data_type     = 0; // Tipo padrão
    header.fragmentation = 0; // Sem fragmentação

    // Calcular CRC simples
    uint8_t crc = 0;
    for (size_t i = 0; i < sizeof(header); i++) {
        crc ^= ((uint8_t *)&header)[i];
    }
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
    }

    // Montar pacote: header + crc + dados
    uint8_t packet[config_.max_packet_size];
    size_t packet_len = 0;

    memcpy(packet, &header, sizeof(header));
    packet_len += sizeof(header);

    packet[packet_len++] = crc;

    memcpy(packet + packet_len, data, length);
    packet_len += length;

    // Enviar via ESP-NOW
    esp_err_t err = esp_now_send(peer->mac_address.data(), packet, packet_len);

    if (err != ESP_OK) {
        snprintf(last_error_, sizeof(last_error_), "Send failed: %s",
                 esp_err_to_name(err));
        return false;
    }

    // Registrar para ACK se necessário
    if (require_ack && config_.ack_timeout > 0) {
        ack_manager_->markAsSent(node_id, header.sequence);
        peer->tx_count++;
    }

    return true;
}

bool EspNowComm::broadcast(const uint8_t *data, size_t length)
{
    if (!config_.allow_broadcast) {
        strncpy(last_error_, "Broadcast not allowed", sizeof(last_error_) - 1);
        return false;
    }

    // MAC de broadcast
    uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    // Preparar cabeçalho (similar ao send, mas dest_id = 0xFF)
    DataHeader header;
    header.version       = 0x01;
    header.type          = MessageType::DATA;
    header.sequence      = ack_manager_->getNextSequence();
    header.timestamp     = esp_timer_get_time() / 1000;
    header.source_id     = node_id_;
    header.dest_id       = 0xFF; // Broadcast
    header.ttl           = 3;    // TTL maior para broadcast
    header.data_length   = length;
    header.data_type     = 0;
    header.fragmentation = 0;

    // Montar pacote
    uint8_t packet[config_.max_packet_size];
    size_t packet_len = sizeof(header) + 1 + length; // header + crc + data

    if (packet_len > config_.max_packet_size) {
        strncpy(last_error_, "Packet too large for broadcast", sizeof(last_error_) - 1);
        return false;
    }

    memcpy(packet, &header, sizeof(header));
    packet[sizeof(header)] = 0; // CRC para broadcast
    memcpy(packet + sizeof(header) + 1, data, length);

    esp_err_t err = esp_now_send(broadcast_mac, packet, packet_len);

    if (err != ESP_OK) {
        snprintf(last_error_, sizeof(last_error_), "Broadcast failed: %s",
                 esp_err_to_name(err));
        return false;
    }

    return true;
}

bool EspNowComm::addPeer(uint8_t node_id,
                         const uint8_t *mac,
                         uint8_t channel,
                         bool encrypt)
{
    // Verificar se já existe
    for (auto &peer : peers_) {
        if (peer.node_id == node_id) {
            snprintf(last_error_, sizeof(last_error_),
                     "Peer with node_id %u already exists", node_id);
            return false;
        }

        if (memcmp(peer.mac_address.data(), mac, 6) == 0) {
            snprintf(last_error_, sizeof(last_error_),
                     "Peer with this MAC already exists");
            return false;
        }
    }

    // Verificar limite
    if (peers_.size() >= config_.max_peers) {
        snprintf(last_error_, sizeof(last_error_), "Maximum peers reached (%u)",
                 config_.max_peers);
        return false;
    }

    // Adicionar peer ao ESP-NOW
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
        snprintf(last_error_, sizeof(last_error_), "Failed to add peer to ESP-NOW: %s",
                 esp_err_to_name(err));
        return false;
    }

    // Adicionar à nossa lista
    PeerInfo new_peer;
    new_peer.node_id = node_id;
    memcpy(new_peer.mac_address.data(), mac, 6);
    new_peer.alias              = "peer_" + std::to_string(node_id);
    new_peer.first_seen         = esp_timer_get_time() / 1000;
    new_peer.last_seen          = new_peer.first_seen;
    new_peer.last_rtt           = 0;
    new_peer.tx_count           = 0;
    new_peer.rx_count           = 0;
    new_peer.tx_failures        = 0;
    new_peer.rx_errors          = 0;
    new_peer.last_rssi          = 0;
    new_peer.avg_rssi           = 0;
    new_peer.link_quality       = 100;
    new_peer.is_confirmed       = true;
    new_peer.is_encrypted       = encrypt;
    new_peer.is_active          = true;
    new_peer.is_broadcast       = false;
    new_peer.preferred_channel  = peer_info.channel;
    new_peer.heartbeat_interval = config_.heartbeat_interval;
    new_peer.last_heartbeat     = new_peer.first_seen;

    peers_.push_back(new_peer);

    // Notificar callback
    if (on_peer_event_) {
        on_peer_event_(node_id, mac, true);
    }

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
    for (auto it = peers_.begin(); it != peers_.end(); ++it) {
        if (it->node_id == node_id) {
            // Remover do ESP-NOW
            esp_now_del_peer(it->mac_address.data());

            // Notificar callback
            if (on_peer_event_) {
                on_peer_event_(node_id, it->mac_address.data(), false);
            }

            if (persistence_enabled_) {
                savePeersToRTC();
                savePeersToNVS();
            }

            ESP_LOGI(TAG, "Peer removed: node_id=%u", node_id);

            peers_.erase(it);
            return true;
        }
    }

    snprintf(last_error_, sizeof(last_error_), "Peer %u not found", node_id);
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

bool EspNowComm::startDiscovery(uint32_t timeout_ms)
{
    if (discovery_active_) {
        strncpy(last_error_, "Discovery already active", sizeof(last_error_) - 1);
        return false;
    }

    if (!config_.enable_discovery) {
        strncpy(last_error_, "Discovery not enabled in config", sizeof(last_error_) - 1);
        return false;
    }

    discovery_active_   = true;
    discovery_end_time_ = (esp_timer_get_time() / 1000) + timeout_ms;

    ESP_LOGI(TAG, "Discovery started for %u ms", timeout_ms);
    return true;
}

void EspNowComm::stopDiscovery()
{
    if (!discovery_active_)
        return;

    discovery_active_ = false;
    ESP_LOGI(TAG, "Discovery stopped");
}

void EspNowComm::process()
{
    if (!initialized_)
        return;

    // 1. Processar timeouts de ACKs
    auto timeouts = ack_manager_->checkTimeouts();
    for (auto seq : timeouts) {
        // TODO: Notificar aplicação sobre timeout
        ESP_LOGW(TAG, "ACK timeout for sequence %u", seq);
    }

    // 2. Verificar se discovery expirou
    if (discovery_active_) {
        uint32_t now = esp_timer_get_time() / 1000;
        if (now > discovery_end_time_) {
            stopDiscovery();
        }
    }

    // 3. Enviar heartbeats periódicos
    static uint32_t last_heartbeat = 0;
    uint32_t now                   = esp_timer_get_time() / 1000;

    if (config_.heartbeat_interval > 0 &&
        now - last_heartbeat > config_.heartbeat_interval) {
        sendHeartbeat();
        last_heartbeat = now;
    }

    // 4. Limpar peers inativos
    if (config_.peer_timeout > 0) {
        cleanupInactivePeers();
    }
}

// Métodos privados
void EspNowComm::handleReceive(const uint8_t *mac, const uint8_t *data, int len)
{
    if (len < (int)sizeof(MessageHeader) + 1) {
        ESP_LOGW(TAG, "Packet too short: %d bytes", len);
        return;
    }

    MessageHeader *header = (MessageHeader *)data;
    uint8_t received_crc  = data[sizeof(MessageHeader)];

    // Validar CRC básico
    uint8_t expected_crc = 0;
    for (int i = 0; i < sizeof(MessageHeader); i++) {
        expected_crc ^= data[i];
    }
    for (int i = sizeof(MessageHeader) + 1; i < len; i++) {
        expected_crc ^= data[i];
    }

    if (received_crc != expected_crc) {
        ESP_LOGW(TAG, "Invalid CRC: got %02X, expected %02X", received_crc, expected_crc);
        return;
    }

    // Encontrar ou criar peer
    PeerInfo *peer = findPeerByMac(mac);
    if (!peer && config_.auto_pairing && header->type != MessageType::PAIR_REQUEST) {
        // Auto-pareamento: criar peer temporário
        if (peers_.size() < config_.max_peers) {
            PeerInfo new_peer;
            memcpy(new_peer.mac_address.data(), mac, 6);
            new_peer.node_id      = header->source_id;
            new_peer.alias        = "auto_peer";
            new_peer.first_seen   = esp_timer_get_time() / 1000;
            new_peer.last_seen    = new_peer.first_seen;
            new_peer.rx_count     = 1;
            new_peer.is_confirmed = false;
            new_peer.is_active    = true;

            peers_.push_back(new_peer);
            peer = &peers_.back();

            ESP_LOGI(TAG, "Auto-paired with node %u", header->source_id);
        }
    }

    if (peer) {
        peer->last_seen = esp_timer_get_time() / 1000;
        peer->rx_count++;
        peer->is_active = true;
    }

    // Processar tipo de mensagem
    switch (header->type) {
    case MessageType::DATA:
        if (header->dest_id == node_id_ || header->dest_id == 0xFF) {
            // Enviar ACK se não for broadcast
            if (header->dest_id != 0xFF) {
                sendAck(mac, header->sequence);
            }

            // Chamar callback da aplicação
            if (on_receive_) {
                const uint8_t *payload = data + sizeof(MessageHeader) + 1;
                size_t payload_len     = len - sizeof(MessageHeader) - 1;
                on_receive_(mac, payload, payload_len);
            }
        }
        break;

    case MessageType::ACK:
        if (ack_manager_->markAsAcknowledged(header->sequence)) {
            ESP_LOGD(TAG, "ACK received for sequence %u", header->sequence);
        }
        break;

    case MessageType::PAIR_REQUEST:
        if (config_.auto_pairing) {
            // TODO: Processar pedido de pareamento
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
}

void EspNowComm::handleSend(const uint8_t *mac, esp_now_send_status_t status)
{
    PeerInfo *peer = findPeerByMac(mac);
    if (peer) {
        if (status == ESP_NOW_SEND_SUCCESS) {
            peer->link_quality = std::min(100, peer->link_quality + 1);
        }
        else {
            peer->tx_failures++;
            peer->link_quality = std::max(0, peer->link_quality - 5);
        }
    }

    if (on_send_) {
        on_send_(mac, status);
    }
}

// Callbacks estáticos
void EspNowComm::espNowSendCb(const esp_now_send_info_t *tx_info,
                              esp_now_send_status_t status)
{
    if (instance_) {
        // Extrai o MAC address da estrutura wifi_tx_info_t
        instance_->handleSend(tx_info->des_addr, status);
    }
}

void EspNowComm::espNowRecvCb(const esp_now_recv_info_t *recv_info,
                              const uint8_t *data,
                              int len)
{
    if (instance_ && recv_info) {
        instance_->handleReceive(recv_info->src_addr, data, len);
    }
}

// Métodos auxiliares privados
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
    AckHeader ack;
    ack.version        = 0x01;
    ack.type           = MessageType::ACK;
    ack.sequence       = ack_manager_->getNextSequence();
    ack.timestamp      = esp_timer_get_time() / 1000;
    ack.source_id      = node_id_;
    ack.dest_id        = 0xFF; // Será substituído pelo MAC
    ack.ttl            = 1;
    ack.acked_sequence = sequence;
    ack.rssi           = 0; // TODO: Obter RSSI real
    ack.error_code     = ErrorCode::NONE;

    uint8_t packet[sizeof(ack) + 1];
    memcpy(packet, &ack, sizeof(ack));

    // CRC simples
    uint8_t crc = 0;
    for (size_t i = 0; i < sizeof(ack); i++) {
        crc ^= packet[i];
    }
    packet[sizeof(ack)] = crc;

    esp_now_send(mac, packet, sizeof(packet));
}

void EspNowComm::sendHeartbeat()
{
    HeartbeatHeader hb;
    hb.version       = 0x01;
    hb.type          = MessageType::HEARTBEAT;
    hb.sequence      = ack_manager_->getNextSequence();
    hb.timestamp     = esp_timer_get_time() / 1000;
    hb.source_id     = node_id_;
    hb.dest_id       = 0xFF; // Broadcast
    hb.ttl           = 1;
    hb.battery_level = 1000; // 100% (suposição)
    hb.status_flags  = 0;
    hb.free_heap     = esp_get_free_heap_size() / 1024;

    uint8_t packet[sizeof(hb) + 1];
    memcpy(packet, &hb, sizeof(hb));

    // CRC
    uint8_t crc = 0;
    for (size_t i = 0; i < sizeof(hb); i++) {
        crc ^= packet[i];
    }
    packet[sizeof(hb)] = crc;

    // Enviar para todos os peers
    for (auto &peer : peers_) {
        if (peer.is_active) {
            esp_now_send(peer.mac_address.data(), packet, sizeof(packet));
        }
    }
}

void EspNowComm::cleanupInactivePeers()
{
    uint32_t now = esp_timer_get_time() / 1000;

    for (auto it = peers_.begin(); it != peers_.end();) {
        if (!it->is_confirmed && (now - it->last_seen > config_.peer_timeout)) {
            ESP_LOGI(TAG, "Removing inactive peer: node_id=%u", it->node_id);

            if (on_peer_event_) {
                on_peer_event_(it->node_id, it->mac_address.data(), false);
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
    if (!persistence_enabled_)
        return false;

    // 1. Try RTC first (fast)
    auto rtc_peers = PeerPersistence::loadFromRTC();

    if (!rtc_peers.empty()) {
        ESP_LOGI(TAG, "Using peers from RTC memory (%zu peers)", rtc_peers.size());
    }
    else {
        // 2. Fallback to NVS (slower)
        auto nvs_peers = PeerPersistence::loadFromNVS();

        if (!nvs_peers.empty()) {
            ESP_LOGI(TAG, "Using peers from NVS (%zu peers)", nvs_peers.size());

            // Update RTC with NVS data for next time
            PeerPersistence::saveToRTC(nvs_peers);
            rtc_peers = nvs_peers;
        }
        else {
            ESP_LOGI(TAG, "No persisted peers found");
            return false;
        }
    }

    // Add peers to ESP-NOW
    for (const auto &peer : rtc_peers) {
        // Use default channel (0) and no encryption for persisted peers
        addPeer(peer.node_id, peer.mac, 0, false);
    }

    return true;
}

std::vector<PeerPersistence::PersistentPeer> EspNowComm::getPersistentPeers() const
{
    std::vector<PeerPersistence::PersistentPeer> peers;

    for (const auto &peer : peers_) {
        if (peer.is_confirmed) { // Only save confirmed peers
            PeerPersistence::PersistentPeer p;
            memcpy(p.mac, peer.mac_address.data(), 6);
            p.node_id = peer.node_id;
            peers.push_back(p);
        }
    }

    return peers;
}

bool EspNowComm::savePeersToNVS()
{
    if (!persistence_enabled_ || !initialized_)
        return false;

    auto peers = getPersistentPeers();
    if (peers.empty()) {
        ESP_LOGD(TAG, "No peers to save to NVS");
        return true;
    }

    bool success = PeerPersistence::saveToNVS(peers);
    if (success) {
        ESP_LOGI(TAG, "Saved %zu peers to NVS", peers.size());
    }

    return success;
}

bool EspNowComm::savePeersToRTC()
{
    if (!persistence_enabled_ || !initialized_)
        return false;

    auto peers = getPersistentPeers();
    if (peers.empty()) {
        ESP_LOGD(TAG, "No peers to save to RTC");
        return true;
    }

    bool success = PeerPersistence::saveToRTC(peers);
    if (success) {
        ESP_LOGD(TAG, "Saved %zu peers to RTC", peers.size());
    }

    return success;
}

// Setters para callbacks
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

size_t EspNowComm::getPeerCount() const
{
    return peers_.size();
}

const char *EspNowComm::getLastError() const
{
    return last_error_;
}