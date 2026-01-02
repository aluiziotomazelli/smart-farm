// message_types.hpp
#pragma once

#include "common_types.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <string>

// ====================================================
// MESSAGE TYPE DEFINITIONS
// ====================================================

/**
 * @enum MessageType
 * @brief Defines the different types of messages used in the ESP-NOW protocol.
 */
enum class MessageType : uint8_t
{
    DATA          = 0x01, ///< Contains application-specific payload.
    ACK           = 0x02, ///< Acknowledges the receipt of a message.
    PAIR_REQUEST  = 0x03, ///< Initiates a pairing sequence with another device.
    PAIR_RESPONSE = 0x04, ///< Responds to a pairing request.
    HEARTBEAT     = 0x05, ///< A keep-alive signal to maintain peer status.
    ERROR         = 0x06, ///< Notifies about a protocol or communication error.
    COMMAND       = 0x07, ///< A control command for the remote device.
    OTA           = 0x08  ///< A command to initiate an Over-the-Air update.
};

// ====================================================
// ERROR CODES
// ====================================================

/**
 * @enum ErrorCode
 * @brief Defines error codes for communication failures, sent within ACK messages.
 */
enum class ErrorCode : uint8_t
{
    NONE              = 0, ///< No error.
    INVALID_CRC       = 1, ///< The received message failed its CRC check.
    INVALID_SEQUENCE  = 2, ///< The sequence number was unexpected.
    PEER_NOT_FOUND    = 3, ///< The destination peer is not registered.
    BUFFER_FULL       = 4, ///< The receive buffer is full.
    TIMEOUT           = 5, ///< The operation timed out.
    ENCRYPTION_FAILED = 6  ///< Encryption or decryption failed.
};

// ====================================================
// MESSAGE HEADERS (PACKED STRUCTURES)
// ====================================================

#pragma pack(push, 1) // Disable structure padding to ensure consistent memory layout.

/**
 * @struct MessageHeader
 * @brief The base header included in all messages.
 */
struct MessageHeader
{
    uint8_t version;    ///< Protocol version (e.g., 0x01).
    MessageType type;   ///< The type of the message.
    uint16_t sequence;  ///< Sequence number for message ordering and ACKs.
    uint32_t timestamp; ///< Timestamp in milliseconds.
    uint8_t source_id;  ///< The node_id of the sender.
    uint8_t dest_id;    ///< The node_id of the recipient (0xFF for broadcast).
    uint8_t ttl;        ///< Time To Live, for mesh network routing (not yet implemented).
};

/**
 * @struct DataHeader
 * @brief Extended header for standard data messages.
 */
struct DataHeader : public MessageHeader
{
    uint16_t data_length;  ///< The length of the payload data.
    uint8_t data_type;     ///< Application-specific data type identifier.
    uint8_t fragmentation; ///< Flags for message fragmentation (not yet implemented).
};

/**
 * @struct AckHeader
 * @brief Header for acknowledgment messages.
 */
struct AckHeader : public MessageHeader
{
    uint16_t acked_sequence; ///< The sequence number of the message being acknowledged.
    uint8_t
        rssi; ///< The RSSI (Received Signal Strength Indicator) of the received message.
    ErrorCode error_code; ///< An error code, if any.
};

/**
 * @struct PairHeader
 * @brief Header for pairing request/response messages.
 */
struct PairHeader : public MessageHeader
{
    char device_name[16];  ///< A human-readable name for the device.
    NodeType node_type;    ///< The type of the node sending the request.
    uint8_t auth_token[8]; ///< An authentication token, if required.
};

/**
 * @struct HeartbeatHeader
 * @brief Header for heartbeat messages.
 */
struct HeartbeatHeader : public MessageHeader
{
    uint16_t battery_level; ///< Battery level (e.g., 0-1000 for 0-100.0%).
    uint8_t status_flags;   ///< A bitmask for various device status flags.
    uint8_t free_heap;      ///< Free heap memory in kilobytes.
};

/**
 * @struct OtaCommand
 * @brief Payload for the OTA message type.
 */
struct OtaCommand
{
    char url[128];     ///< The URL of the firmware binary.
    char ssid[32];     ///< The WiFi SSID to connect to. Can be empty to use stored
                       ///< credentials.
    char password[64]; ///< The WiFi password.
};

#pragma pack(pop) // Restore default padding.

// ====================================================
// PEER INFORMATION STRUCTURE
// ====================================================

/**
 * @struct PeerInfo
 * @brief Holds all relevant information and statistics about a peer.
 */
struct PeerInfo
{
    // Identification
    uint8_t node_id;                    ///< The peer's unique node ID.
    std::array<uint8_t, 6> mac_address; ///< The peer's MAC address.
    std::string alias;                  ///< A human-readable alias for the peer.

    // Status tracking
    uint32_t first_seen; ///< Timestamp (ms) when the peer was first seen.
    uint32_t last_seen;  ///< Timestamp (ms) of the last received message.
    uint32_t last_rtt;   ///< The last measured Round-Trip Time (ms).

    // Statistics
    uint32_t tx_count;    ///< Total messages sent to this peer.
    uint32_t rx_count;    ///< Total messages received from this peer.
    uint32_t tx_failures; ///< Number of failed transmission attempts.
    uint32_t rx_errors;   ///< Number of received messages with errors.

    // Connection quality
    int8_t last_rssi;     ///< The most recent RSSI measurement.
    int8_t avg_rssi;      ///< An exponential moving average of the RSSI.
    uint8_t link_quality; ///< A link quality score (0-100%).

    // State flags
    bool is_confirmed; ///< True if the peer has been confirmed through pairing.
    bool is_encrypted; ///< True if communication with this peer is encrypted.
    bool is_active;    ///< True if the peer is considered currently active.
    bool is_broadcast; ///< A special flag for the broadcast address.

    // Configuration
    uint8_t preferred_channel;   ///< The preferred WiFi channel for this peer.
    uint32_t heartbeat_interval; ///< The expected heartbeat interval in ms.
    uint32_t last_heartbeat;     ///< Timestamp (ms) of the last received heartbeat.
};

// ====================================================
// CONFIGURATION STRUCTURE
// ====================================================

/**
 * @struct ESPNOWConfig
 * @brief Configuration parameters for the EspNowComm class.
 */
struct ESPNOWConfig
{
    // Identity parameters
    uint8_t node_id;    ///< The unique ID of this node.
    NodeType node_type; ///< The type of this node (e.g., HUB, SENSOR).

    // Network parameters
    uint8_t wifi_channel;   ///< WiFi channel to operate on (1-13). 0 for current.
    bool enable_long_range; ///< Enable ESP-IDF's long-range mode.

    // Protocol parameters
    uint16_t max_packet_size; ///< Maximum packet size in bytes.
    uint8_t max_peers;        ///< Maximum number of peers to track.
    uint8_t max_retries;      ///< Maximum transmission retries for ACK'd messages.

    // Timeout parameters (milliseconds)
    uint32_t ack_timeout;        ///< Time to wait for an ACK.
    uint32_t heartbeat_interval; ///< Interval to send heartbeats.
    uint32_t peer_timeout; ///< Time of inactivity before a peer is considered inactive.
    uint32_t discovery_timeout; ///< Duration for the discovery process.

    // Security settings
    bool enable_encryption;      ///< Enable ESP-NOW's built-in encryption.
    std::array<uint8_t, 16> pmk; ///< Primary Master Key for encryption.
    std::array<uint8_t, 16> lmk; ///< Local Master Key for encryption.

    // Feature flags
    bool enable_discovery;  ///< Enable the peer discovery feature.
    bool enable_statistics; ///< Enable tracking of detailed connection statistics.

    /**
     * @brief Default constructor with sensible defaults.
     */
    ESPNOWConfig()
        : node_id(0)
        , node_type(NodeType::UNKNOWN)
        , wifi_channel(0)
        , enable_long_range(false)
        , max_packet_size(250)
        , max_peers(20)
        , max_retries(3)
        , ack_timeout(100)
        , heartbeat_interval(5000)
        , peer_timeout(30000)
        , discovery_timeout(10000)
        , enable_encryption(false)
        , enable_discovery(true)
        , enable_statistics(true)
    {
        pmk.fill(0);
        lmk.fill(0);
    }
};

// ====================================================
// CALLBACK FUNCTION TYPES
// ====================================================

/**
 * @brief Callback for received application data.
 * @param sender_id The node_id of the sender.
 * @param data Pointer to the payload data.
 * @param length The length of the payload data.
 * @param rssi The Received Signal Strength Indicator.
 */
using MessageCallback = std::function<
    void(uint8_t sender_id, const uint8_t *data, size_t length, int8_t rssi)>;

/**
 * @brief Callback for message delivery status.
 * @param recipient_id The node_id of the recipient.
 * @param success True if the message was delivered successfully (acknowledged).
 * @param delivery_time Time taken for delivery and acknowledgment in ms.
 */
using DeliveryCallback =
    std::function<void(uint8_t recipient_id, bool success, uint32_t delivery_time)>;

/**
 * @brief Callback for peer events (e.g., added, removed, updated).
 * @param peer The information structure of the peer.
 * @param event_type A string describing the event (e.g., "added", "removed").
 */
using PeerEventCallback =
    std::function<void(const PeerInfo &peer, const char *event_type)>;
