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

/**
 * @class EspNowComm
 * @brief Manages ESP-NOW communication with a custom, reliable protocol.
 *
 * This class provides a comprehensive solution for ESP-NOW communication,
 * featuring a rich protocol with acknowledgments, retries, peer management,
 * persistence, and more. It is designed to be thread-safe.
 */
class EspNowComm
{
public:
    /**
     * @brief Constructs the EspNowComm instance.
     * @param enable_persistence If true, peer data will be saved to and loaded from
     * NVS/RTC.
     */
    EspNowComm(bool enable_persistence = true);

    /**
     * @brief Destructor. Cleans up resources.
     */
    ~EspNowComm();

    // Disable copy constructor and assignment operator
    EspNowComm(const EspNowComm &)            = delete;
    EspNowComm &operator=(const EspNowComm &) = delete;

    /**
     * @brief Initializes the ESP-NOW communication stack.
     * @param config The configuration parameters for the component.
     * @return true if initialization is successful, false otherwise.
     */
    bool init(const ESPNOWConfig &config);

    /**
     * @brief Deinitializes the ESP-NOW communication stack.
     */
    void deinit();

    /**
     * @brief Gets the node ID of this device.
     * @return This device's node ID.
     */
    uint8_t get_id() const;

    /**
     * @brief Sends data to a specific peer.
     * @param node_id The destination node ID.
     * @param data A pointer to the data buffer.
     * @param length The length of the data in bytes.
     * @param require_ack If true, the component will expect an acknowledgment.
     * @return true if the message was successfully queued for sending, false on error.
     */
    bool send(uint8_t node_id,
              const uint8_t *data,
              size_t length,
              bool require_ack = true);

    /**
     * @brief Sends a broadcast message to all peers.
     * @param data A pointer to the data buffer.
     * @param length The length of the data in bytes.
     * @return true if the message was successfully queued for sending, false on error.
     */
    bool broadcast(const uint8_t *data, size_t length);

    /**
     * @brief Adds a peer to the peer list for communication.
     * @param node_id The peer's unique node ID.
     * @param mac The peer's MAC address (6 bytes).
     * @param channel The WiFi channel of the peer (0 for current channel).
     * @param encrypt If true, enables encryption for this peer.
     * @return true if the peer was added successfully, false otherwise.
     */
    bool addPeer(uint8_t node_id,
                 const uint8_t *mac,
                 uint8_t channel = 0,
                 bool encrypt    = false);

    /**
     * @brief Removes a peer from the peer list.
     * @param node_id The node ID of the peer to remove.
     * @return true if the peer was removed, false if it was not found.
     */
    bool removePeer(uint8_t node_id);

    /**
     * @brief Retrieves information about a specific peer.
     * @param node_id The node ID of the peer.
     * @return A constant pointer to the PeerInfo struct if found, otherwise nullptr.
     */
    const PeerInfo *getPeerInfo(uint8_t node_id) const;

    /**
     * @brief Starts the peer discovery process.
     * @param timeout_ms The duration for the discovery in milliseconds.
     * @return true if discovery was started, false if already running or disabled.
     */
    bool startDiscovery(uint32_t timeout_ms = 10000);

    /**
     * @brief Stops the peer discovery process.
     */
    void stopDiscovery();

    /**
     * @brief Main processing function. Must be called periodically in the application's
     * main loop.
     */
    void process();

    // Callback types
    using OnReceiveCallback =
        std::function<void(uint8_t node_id, const uint8_t *data, int len, int8_t rssi)>;
    using OnSendCallback =
        std::function<void(uint8_t node_id, esp_now_send_status_t status)>;
    using OnPeerEventCallback = std::function<void(const PeerInfo &peer, bool added)>;

    void setReceiveCallback(OnReceiveCallback callback);
    void setSendCallback(OnSendCallback callback);
    void setPeerEventCallback(OnPeerEventCallback callback);

    size_t getPeerCount() const;
    const char *getLastError() const;

    // Manual persistence control
    bool savePeersToNVS();
    bool savePeersToRTC();

private:
    // Internal handler methods
    void handleReceive(const esp_now_recv_info_t *recv_info,
                       const uint8_t *data,
                       int len);
    void handleSend(const esp_now_send_info_t *tx_info, esp_now_send_status_t status);

    // Peer management helpers
    PeerInfo *findPeerByMac(const uint8_t *mac);
    PeerInfo *findPeerById(uint8_t node_id);

    // Internal protocol logic
    void sendAck(const uint8_t *mac, uint16_t sequence);
    void sendPairRequest();
    void sendPairResponse(const uint8_t *mac, const PairHeader &request_header);
    void sendHeartbeat();
    void cleanupInactivePeers();
    bool loadPeersIntelligently();
    std::vector<PeerPersistence::PersistentPeer> getPersistentPeers() const;

    // Static callbacks passed to the ESP-IDF ESP-NOW API
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

    AcknowledgmentManager ack_manager_; // Object member, not a pointer

    // Discovery state
    bool discovery_active_;
    uint32_t discovery_end_time_;

    // Error tracking
    char last_error_[64];

    // Thread safety
    SemaphoreHandle_t mutex_; // Mutex for protecting shared data (peers_, ack_manager_)

    // Singleton-like instance pointer for static callbacks
    static EspNowComm *instance_;
};
