#pragma once
#include "comm_types.hpp"
#include "nvs_core.hpp"
#include "protocol_frame.hpp"

#include <cstdint>

namespace comm {

class CommInterface
{
public:
    virtual ~CommInterface() = default;

    // Lifecycle
    virtual bool       init()         = 0;
    virtual bool       start()        = 0;
    virtual void       stop()         = 0;
    virtual CommStatus status() const = 0;
    virtual void       reset()        = 0;

    // Core send/receive
    virtual bool send(const protocol::Frame &frame) = 0;
    virtual bool has_message() const                = 0;
    virtual bool receive(protocol::Frame &out)      = 0;

    // Peer management / discovery
    virtual bool resolve_peer(uint32_t node_id, uint8_t out_mac[6])    = 0;
    virtual void start_discovery(uint32_t target_node_id = 0xFFFFFFFF) = 0;
    virtual void stop_discovery()                                      = 0;

    // Persistence
    virtual bool load() = 0;
    virtual bool save() = 0;
    void         attach_nvs(NvsCore *nvs)
    {
        m_nvs = nvs;
    }

protected:
    comm::CommStatus m_status     = comm::CommStatus::UNINITIALIZED;
    comm::CommError  m_last_error = comm::CommError::NONE;
    comm::CommStats  m_stats;

    NvsCore *m_nvs = nullptr;
};

} // namespace comm
