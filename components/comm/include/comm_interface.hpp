#pragma once

#include <cstdint>
#include <functional>

namespace comm {

class CommInterface
{
public:
    static CommInterface& get_default_instance();

    using RxCallback = std::function<void(uint32_t source_node_id, const uint8_t* payload, size_t len)>;

    virtual ~CommInterface() = default;

    // Lifecycle
    virtual bool init(uint32_t node_id) = 0;
    virtual void stop() = 0;

    // Core send/receive
    virtual bool send(uint32_t destination_node_id, const uint8_t* payload, size_t len) = 0;
    virtual void set_rx_callback(RxCallback cb) = 0;
};

} // namespace comm
