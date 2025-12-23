// acknowledgment_manager.cpp
#include "acknowledgment_manager.hpp"
#include <vector>

AcknowledgmentManager::AcknowledgmentManager(uint32_t ack_timeout_ms, uint8_t max_retries)
    : ack_timeout_us_(ack_timeout_ms * 1000) // Convert ms to us
    , max_retries_(max_retries)
    , next_sequence_(1)
{
}

bool AcknowledgmentManager::markAsSent(uint8_t dest_id, uint16_t sequence)
{
    // Check duplicate
    if (pending_acks_.find(sequence) != pending_acks_.end()) {
        return false;
    }

    // Add to pending
    PendingAck pending;
    pending.sequence       = sequence;
    pending.timestamp_us   = esp_timer_get_time();
    pending.destination_id = dest_id;
    pending.retries        = 0;

    pending_acks_[sequence] = pending;
    return true;
}

bool AcknowledgmentManager::markAsAcknowledged(uint16_t sequence)
{
    auto it = pending_acks_.find(sequence);
    if (it != pending_acks_.end()) {
        pending_acks_.erase(it);
        return true;
    }
    return false;
}

std::vector<uint16_t> AcknowledgmentManager::checkTimeouts()
{
    std::vector<uint16_t> timed_out;
    int64_t               current_time_us = esp_timer_get_time();

    for (auto it = pending_acks_.begin(); it != pending_acks_.end();) {
        PendingAck &pending = it->second;

        if (current_time_us - pending.timestamp_us > ack_timeout_us_) {
            pending.retries++;

            if (pending.retries >= max_retries_) {
                // Max retries reached - give up
                timed_out.push_back(pending.sequence);
                it = pending_acks_.erase(it);
            }
            else {
                // Schedule retry
                pending.timestamp_us = current_time_us;
                ++it;
            }
        }
        else {
            ++it;
        }
    }

    return timed_out;
}

uint16_t AcknowledgmentManager::getNextSequence()
{
    return next_sequence_++;
}

void AcknowledgmentManager::reset()
{
    pending_acks_.clear();
    next_sequence_ = 1;
}

bool AcknowledgmentManager::isPending(uint16_t sequence) const
{
    return pending_acks_.find(sequence) != pending_acks_.end();
}

size_t AcknowledgmentManager::getPendingCount() const
{
    return pending_acks_.size();
}