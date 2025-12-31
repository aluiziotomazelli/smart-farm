#pragma once

#include "acknowledgment_manager.hpp" // Included because it's now a member object
#include "message_types.hpp"
#include "persistence.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "esp_now.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h" // Included for the mutex handle

class EspNowComm
{
public:
    EspNowComm(bool enable_persistence = true);
    ~EspNowComm();

    EspNowComm(const EspNowComm &)            = delete;
    EspNowComm &operator=(const EspNowComm &) = delete;

    bool init(const ESPNOWConfig &config);
    void deinit();
    uint8_t get_id() const;

    void pauseForOta();
    void resumeAfterOta();

    bool send(uint8_t node_id,
              const uint8_t *data,
              size_t length,
              bool require_ack = true);
    bool broadcast(const uint8_t *data, size_t length);

    bool addPeer(uint8_t node_id,
                 const uint8_t *mac,
                 uint8_t channel = 0,
                 bool encrypt    = false);
    bool removePeer(uint8_t node_id);
    const PeerInfo *getPeerInfo(uint8_t node_id) const;
    std::vector<PeerPersistence::PersistentPeer> getPeers() const;

    bool startDiscovery(uint32_t timeout_ms = 10000);
    void stopDiscovery();
    void process();

    using OnReceiveCallback =
        std::function<void(uint8_t node_id, const uint8_t *data, int len, int8_t rssi)>;
    using OnSendCallback =
        std::function<void(uint8_t node_id, esp_now_send_status_t status)>;
    using OnPeerEventCallback  = std::function<void(const PeerInfo &peer, bool added)>;
    using OnAckSuccessCallback = std::function<void(uint8_t node_id)>;
    using OnAckTimeoutCallback = std::function<void(uint8_t node_id)>;

    void setReceiveCallback(OnReceiveCallback callback);
    void setSendCallback(OnSendCallback callback);
    void setPeerEventCallback(OnPeerEventCallback callback);
    void setAckSuccessCallback(OnAckSuccessCallback callback);
    void setAckTimeoutCallback(OnAckTimeoutCallback callback);

    size_t getPeerCount() const;
    const char *getLastError() const;

    bool savePeersToNVS();
    bool savePeersToRTC();
    void cleanupInactivePeers();

private:
    void handleReceive(const esp_now_recv_info_t *recv_info,
                       const uint8_t *data,
                       int len);
    void handleSend(const esp_now_send_info_t *tx_info, esp_now_send_status_t status);

    PeerInfo *findPeerByMac(const uint8_t *mac);
    PeerInfo *findPeerById(uint8_t node_id);

    bool addPeerInternal(uint8_t node_id,
                         const uint8_t *mac,
                         uint8_t channel,
                         bool encrypt);

    void sendAck(const uint8_t *mac, uint16_t sequence);
    void sendPairRequest();
    void sendPairResponse(const uint8_t *mac, const PairHeader &request_header);
    void sendHeartbeat();

    void cleanupInactivePeersInternal();
    bool loadPeers();
    std::vector<PeerPersistence::PersistentPeer> getPeersInternal() const;
    bool savePeersToNVSInternal();
    bool savePeersToRTCInternal();

    static void espNowRecvCb(const esp_now_recv_info_t *recv_info,
                             const uint8_t *data,
                             int len);
    static void espNowSendCb(const esp_now_send_info_t *tx_info,
                             esp_now_send_status_t status);

    ESPNOWConfig config_;
    uint8_t node_id_;
    std::vector<PeerInfo> peers_;
    bool initialized_;
    bool persistence_enabled_;

    OnReceiveCallback on_receive_;
    OnSendCallback on_send_;
    OnPeerEventCallback on_peer_event_;
    OnAckSuccessCallback on_ack_success_;
    OnAckTimeoutCallback on_ack_timeout_;

    AcknowledgmentManager ack_manager_;

    bool discovery_active_;
    uint32_t discovery_end_time_;

    char last_error_[64];

    SemaphoreHandle_t mutex_;

    static EspNowComm *instance_;
};
