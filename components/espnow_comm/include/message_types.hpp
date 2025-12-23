// message_types.hpp
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>

// ====================================================
// MESSAGE TYPE DEFINITIONS
// ====================================================

/**
 * @brief Message types for the enhanced ESP-NOW protocol
 */
enum class MessageType : uint8_t
{
    DATA          = 0x01, // Application data
    ACK           = 0x02, // Acknowledgment
    PAIR_REQUEST  = 0x03, // Pairing request
    PAIR_RESPONSE = 0x04, // Pairing response
    HEARTBEAT     = 0x05, // Keep-alive heartbeat
    ERROR         = 0x06, // Error notification
    COMMAND       = 0x07  // Control command
};

// ====================================================
// ERROR CODES
// ====================================================

/**
 * @brief Error codes for communication failures
 */
enum class ErrorCode : uint8_t
{
    NONE              = 0,
    INVALID_CRC       = 1, // CRC check failed
    INVALID_SEQUENCE  = 2, // Sequence number mismatch
    PEER_NOT_FOUND    = 3, // Peer not in peer list
    BUFFER_FULL       = 4, // Receive buffer full
    TIMEOUT           = 5, // Operation timeout
    ENCRYPTION_FAILED = 6  // Encryption/decryption error
};

// ====================================================
// MESSAGE HEADERS (PACKED STRUCTURES)
// ====================================================

#pragma pack(push, 1) // Disable structure padding

/**
 * @brief Base header for all messages
 */
struct MessageHeader
{
    uint8_t version;    // Protocol version (starts at 0x01)
    MessageType type;   // Message type
    uint16_t sequence;  // Sequence number (for ordering)
    uint32_t timestamp; // Millisecond timestamp
    uint8_t source_id;  // Sender's internal ID
    uint8_t dest_id;    // Destination ID (0xFF = broadcast)
    uint8_t ttl;        // Time To Live (hops remaining)
};

/**
 * @brief Extended header for data messages
 */
struct DataHeader : public MessageHeader
{
    uint16_t data_length;  // Length of payload data
    uint8_t data_type;     // Application-specific data type
    uint8_t fragmentation; // Fragmentation flags (bit 0: more fragments, bit 1: first
                           // fragment)
};

/**
 * @brief Header for acknowledgment messages
 */
struct AckHeader : public MessageHeader
{
    uint16_t acked_sequence; // Sequence number being acknowledged
    uint8_t rssi;            // RSSI of the received message
    ErrorCode error_code;    // Error code (if any)
};

/**
 * @brief Header for pairing messages
 */
struct PairHeader : public MessageHeader
{
    char device_name[16];  // Human-readable device name
    uint8_t capabilities;  // Device capabilities bitmask
    uint8_t auth_token[8]; // Authentication token (if required)
};

/**
 * @brief Header for heartbeat messages
 */
struct HeartbeatHeader : public MessageHeader
{
    uint16_t battery_level; // Battery level (0-1000 = 0-100%)
    uint8_t status_flags;   // Device status flags
    uint8_t free_heap;      // Free heap memory in KB
};

#pragma pack(pop) // Restore default padding

// ====================================================
// PEER INFORMATION STRUCTURE
// ====================================================

/**
 * @brief Complete peer information structure
 */
struct PeerInfo
{
    // Identification
    uint8_t node_id;                    // node ID
    std::array<uint8_t, 6> mac_address; // MAC address
    std::string alias;                  // Human-readable name

    // Status tracking
    uint32_t first_seen; // First contact timestamp (ms)
    uint32_t last_seen;  // Last contact timestamp (ms)
    uint32_t last_rtt;   // Last measured Round-Trip Time (ms)

    // Statistics
    uint32_t tx_count;    // Total messages sent to this peer
    uint32_t rx_count;    // Total messages received from this peer
    uint32_t tx_failures; // Failed transmission attempts
    uint32_t rx_errors;   // Received messages with errors

    // Connection quality
    int8_t last_rssi;     // Most recent RSSI measurement
    int8_t avg_rssi;      // Average RSSI (exponential moving average)
    uint8_t link_quality; // Link quality score (0-100%)

    // State flags
    bool is_confirmed; // Pairing confirmed
    bool is_encrypted; // Encryption enabled for this peer
    bool is_active;    // Peer is currently active
    bool is_broadcast; // Broadcast peer (special case)

    // Configuration
    uint8_t preferred_channel;   // Preferred WiFi channel
    uint32_t heartbeat_interval; // Expected heartbeat interval (ms)
    uint32_t last_heartbeat;     // Last heartbeat timestamp
};

// ====================================================
// CONFIGURATION STRUCTURE
// ====================================================

/**
 * @brief Configuration parameters for the enhanced ESP-NOW class
 */
struct ESPNOWConfig
{
    // Network parameters
    uint8_t wifi_channel;   // WiFi channel (0 = auto-select)
    bool enable_long_range; // Enable long-range mode (ESP32 only)

    // Protocol parameters
    uint16_t max_packet_size; // Maximum packet size (bytes)
    uint8_t max_peers;        // Maximum number of peers to track
    uint8_t max_retries;      // Maximum transmission retries

    // Timeout parameters (milliseconds)
    uint32_t ack_timeout;        // ACK wait timeout
    uint32_t heartbeat_interval; // Heartbeat transmission interval
    uint32_t peer_timeout;       // Peer inactivity timeout
    uint32_t discovery_timeout;  // Peer discovery timeout

    // Security settings
    bool enable_encryption;      // Enable encryption (requires PMK/LMK)
    std::array<uint8_t, 16> pmk; // Primary Master Key
    std::array<uint8_t, 16> lmk; // Local Master Key

    // Feature flags
    bool auto_pairing;      // Enable automatic pairing
    bool allow_broadcast;   // Allow broadcast messages
    bool enable_discovery;  // Enable peer discovery
    bool store_peers;       // Store peers to persistent storage
    bool enable_statistics; // Enable connection statistics

    // Default constructor with sensible defaults
    ESPNOWConfig()
        : wifi_channel(0)
        , enable_long_range(false)
        , max_packet_size(250)
        , max_peers(20)
        , max_retries(3)
        , ack_timeout(100)
        , heartbeat_interval(5000)
        , peer_timeout(30000)
        , discovery_timeout(10000)
        , enable_encryption(false)
        , auto_pairing(true)
        , allow_broadcast(true)
        , enable_discovery(true)
        , store_peers(true)
        , enable_statistics(true)
    {
        // Initialize encryption keys to zero
        pmk.fill(0);
        lmk.fill(0);
    }
};

// ====================================================
// CALLBACK FUNCTION TYPES
// ====================================================

/**
 * @brief Callback for received messages
 */
using MessageCallback =
    std::function<void(const uint8_t *sender_id, // Sender's internal ID
                       const uint8_t *data,      // Message payload
                       size_t length,            // Payload length
                       uint8_t rssi              // Received signal strength
                       )>;

/**
 * @brief Callback for message delivery status
 */
using DeliveryCallback =
    std::function<void(const uint8_t *recipient_id, // Recipient's internal ID
                       bool success,                // Delivery success/failure
                       uint32_t delivery_time       // Time taken for delivery (ms)
                       )>;

/**
 * @brief Callback for peer events (add, remove, update)
 */
using PeerEventCallback =
    std::function<void(const PeerInfo &peer,  // Peer information
                       const char *event_type // Event type: "added", "removed", "updated"
                       )>;
