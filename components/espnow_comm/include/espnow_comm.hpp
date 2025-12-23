#pragma once

#include "esp_now.h"
#include "esp_wifi.h"
#include "message_types.hpp"
#include "persistence.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <vector>

/**
 * @brief Enhanced ESP-NOW communication class for ESP32 (ESP-IDF)
 */
class EspNowComm
{
public:
    EspNowComm();
    ~EspNowComm();

    // Constructor with persistence option
    EspNowComm(bool enable_persistence = true);

    // Persistence control
    void enablePersistence(bool enable)
    {
        persistence_enabled_ = enable;
    }

    // Manual save methods
    bool savePeersToNVS(); // Call when adding/removing peers
    bool savePeersToRTC(); // Call before deep sleep

    // Callback types
    using OnReceiveCallback =
        std::function<void(const uint8_t *mac, const uint8_t *data, int len)>;
    using OnSendCallback =
        std::function<void(const uint8_t *mac, esp_now_send_status_t status)>;
    using OnPeerEventCallback =
        std::function<void(uint8_t node_id, const uint8_t *mac, bool added)>;

    /**
     * @brief Initialize ESP-NOW communication
     * @param config Configuration parameters
     * @return true if successful, false otherwise
     */
    bool init(const ESPNOWConfig &config);

    /**
     * @brief Get the node_id of this device
     * @return This device's node_id
     */
    uint8_t get_id() const;

    /**
     * @brief Send data to a specific node
     * @param node_id Destination node_id
     * @param data Pointer to data buffer
     * @param length Data length in bytes
     * @param require_ack Whether to wait for acknowledgment
     * @return true if queued for sending, false on error
     */
    bool send(uint8_t node_id,
              const uint8_t *data,
              size_t length,
              bool require_ack = true);

    /**
     * @brief Send broadcast message
     * @param data Pointer to data buffer
     * @param length Data length in bytes
     * @return true if queued for sending, false on error
     */
    bool broadcast(const uint8_t *data, size_t length);

    /**
     * @brief Add a peer to the peer list
     * @param node_id Peer's node_id
     * @param mac Peer's MAC address (6 bytes)
     * @param channel WiFi channel (0 for current)
     * @param encrypt Enable encryption for this peer
     * @return true if successful, false otherwise
     */
    bool addPeer(uint8_t node_id,
                 const uint8_t *mac,
                 uint8_t channel = 0,
                 bool encrypt    = false);

    /**
     * @brief Remove a peer from the peer list
     * @param node_id Peer's node_id
     * @return true if removed, false if not found
     */
    bool removePeer(uint8_t node_id);

    /**
     * @brief Get peer information
     * @param node_id Peer's node_id
     * @return PeerInfo structure if found, nullptr otherwise
     */
    const PeerInfo *getPeerInfo(uint8_t node_id) const;

    /**
     * @brief Start peer discovery
     * @param timeout_ms Discovery timeout in milliseconds
     * @return true if started, false if already running
     */
    bool startDiscovery(uint32_t timeout_ms = 10000);

    /**
     * @brief Stop peer discovery
     */
    void stopDiscovery();

    /**
     * @brief Process pending tasks (call in main loop)
     */
    void process();

    /**
     * @brief Set receive callback
     * @param callback Function to call when data is received
     */
    void setReceiveCallback(OnReceiveCallback callback);

    /**
     * @brief Set send callback
     * @param callback Function to call when send completes
     */
    void setSendCallback(OnSendCallback callback);

    /**
     * @brief Set peer event callback
     * @param callback Function to call when peers are added/removed
     */
    void setPeerEventCallback(OnPeerEventCallback callback);

    /**
     * @brief Get current peer count
     * @return Number of peers in list
     */
    size_t getPeerCount() const;

    /**
     * @brief Get error string for last operation
     * @return Error description
     */
    const char *getLastError() const;

    /**
     * @brief Deinitialize ESP-NOW
     */
    void deinit();

private:
    // Internal methods
    bool initEspNow();
    bool deinitEspNow();
    void handleReceive(const uint8_t *mac, const uint8_t *data, int len);
    void handleSend(const uint8_t *mac, esp_now_send_status_t status);
    PeerInfo *findPeerByMac(const uint8_t *mac);
    PeerInfo *findPeerById(uint8_t node_id);
    bool sendToMac(const uint8_t *mac,
                   const uint8_t *data,
                   size_t length,
                   bool require_ack);
    void processTimeouts();
    void sendAck(const uint8_t *mac, uint16_t sequence);
    bool validatePacket(const uint8_t *data, size_t length, const MessageHeader &header);
    void sendHeartbeat();
    void cleanupInactivePeers();
    bool loadPeersIntelligently();
    std::vector<PeerPersistence::PersistentPeer> getPersistentPeers() const;

    // Static callbacks for ESP-NOW
    static void espNowRecvCb(const esp_now_recv_info_t *recv_info,
                             const uint8_t *data,
                             int len);
    static void espNowSendCb(const esp_now_send_info_t *tx_info,
                             esp_now_send_status_t status);

    // Member variables
    ESPNOWConfig config_;
    uint8_t node_id_;
    std::vector<PeerInfo> peers_;
    bool initialized_;
    bool persistence_enabled_;

    // Callbacks
    OnReceiveCallback on_receive_;
    OnSendCallback on_send_;
    OnPeerEventCallback on_peer_event_;

    // Acknowledgment manager
    class AcknowledgmentManager *ack_manager_;

    // Discovery state
    bool discovery_active_;
    uint32_t discovery_end_time_;

    // Error tracking
    char last_error_[64];

    // Singleton instance for static callbacks
    static EspNowComm *instance_;
};