// acknowledgment_manager.hpp
#pragma once

#include "esp_timer.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

/**
 * @brief Minimal ACK manager for ESP32 (ESP-IDF)
 */
class AcknowledgmentManager
{
private:
    struct PendingAck
    {
        uint16_t sequence;       // Sequence number
        int64_t  timestamp_us;   // When sent (microseconds)
        uint8_t  destination_id; // Target node_id
        uint8_t  retries;        // Number of retries attempted
    };

    // Simple tracking
    std::unordered_map<uint16_t, PendingAck> pending_acks_;

    // Config
    uint32_t ack_timeout_us_; // Timeout in microseconds
    uint8_t  max_retries_;
    uint16_t next_sequence_;

public:
    /**
     * @brief Constructor
     * @param ack_timeout_ms Timeout in milliseconds
     * @param max_retries Maximum retries
     */
    AcknowledgmentManager(uint32_t ack_timeout_ms = 100, uint8_t max_retries = 3);

    /**
     * @brief Mark message as sent, wait for ACK
     * @param dest_id Destination node_id
     * @param sequence Sequence number
     * @return True if tracked, false if duplicate
     */
    bool markAsSent(uint8_t dest_id, uint16_t sequence);

    /**
     * @brief Mark message as acknowledged
     * @param sequence Sequence number
     * @return True if found and removed
     */
    bool markAsAcknowledged(uint16_t sequence);

    /**
     * @brief Check for timed-out messages
     * @return Vector of sequence numbers that timed out
     */
    std::vector<uint16_t> checkTimeouts();

    /**
     * @brief Get next sequence number
     */
    uint16_t getNextSequence();

    /**
     * @brief Reset all pending ACKs
     */
    void reset();

    /**
     * @brief Check if sequence is pending
     */
    bool isPending(uint16_t sequence) const;

    /**
     * @brief Get count of pending ACKs
     */
    size_t getPendingCount() const;
};