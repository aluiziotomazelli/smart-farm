// acknowledgment_manager.hpp
#pragma once

#include "esp_timer.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

/**
 * @class AcknowledgmentManager
 * @brief Manages pending acknowledgments (ACKs) for reliable message delivery.
 *
 * This class tracks sent messages that require an acknowledgment, handles timeouts,
 * and manages retries. It uses sequence numbers to identify messages.
 */
class AcknowledgmentManager
{
private:
    /**
     * @struct PendingAck
     * @brief Represents a message awaiting acknowledgment.
     */
    struct PendingAck
    {
        uint16_t sequence;       ///< The sequence number of the message.
        int64_t  timestamp_us;   ///< Timestamp in microseconds when the message was sent.
        uint8_t  destination_id; ///< The node_id of the destination peer.
        uint8_t  retries;        ///< Number of retries attempted.
    };

    // A map to store pending acknowledgments, keyed by sequence number.
    std::unordered_map<uint16_t, PendingAck> pending_acks_;

    // Configuration
    uint32_t ack_timeout_us_; ///< Timeout for waiting for an ACK in microseconds.
    uint8_t  max_retries_;    ///< Maximum number of times to retry sending a message.
    uint16_t next_sequence_;  ///< The next sequence number to be used.

public:
    /**
     * @brief Constructs an AcknowledgmentManager.
     * @param ack_timeout_ms The timeout for acknowledgments in milliseconds.
     * @param max_retries The maximum number of send retries before giving up.
     */
    AcknowledgmentManager(uint32_t ack_timeout_ms = 100, uint8_t max_retries = 3);

    /**
     * @brief Marks a message as sent and starts tracking it for an acknowledgment.
     * @param dest_id The destination node_id of the message.
     * @param sequence The sequence number of the message.
     * @return true if the message is now being tracked, false if it was a duplicate.
     */
    bool markAsSent(uint8_t dest_id, uint16_t sequence);

    /**
     * @brief Marks a message as acknowledged, stopping the tracking.
     * @param sequence The sequence number of the acknowledged message.
     * @return true if the message was found and removed from tracking, false otherwise.
     */
    bool markAsAcknowledged(uint16_t sequence);

    /**
     * @brief Checks for messages that have timed out waiting for an ACK.
     * @return A vector of sequence numbers for messages that have timed out.
     */
    std::vector<uint16_t> checkTimeouts();

    /**
     * @brief Gets the next available sequence number.
     * @return A unique sequence number.
     */
    uint16_t getNextSequence();

    /**
     * @brief Resets the manager, clearing all pending ACKs and resetting the sequence number.
     */
    void reset();

    /**
     * @brief Checks if a specific sequence number is currently pending an ACK.
     * @param sequence The sequence number to check.
     * @return true if the sequence number is pending, false otherwise.
     */
    bool isPending(uint16_t sequence) const;

    /**
     * @brief Gets the total number of messages currently awaiting acknowledgment.
     * @return The count of pending ACKs.
     */
    size_t getPendingCount() const;
};
