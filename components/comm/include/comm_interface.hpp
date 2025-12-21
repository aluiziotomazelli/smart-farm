#pragma once

#include "comm_types.hpp"

namespace comm {

class CommInterface
{
public:
    // lifecycle
    virtual bool init()  = 0;
    virtual bool start() = 0;
    virtual void stop()  = 0;

    // state
    virtual bool       is_ready() const     = 0;
    virtual bool       is_connected() const = 0;
    virtual CommStatus status() const       = 0;

    // send / receive
    virtual bool send(const CommMessage &msg) = 0;
    virtual bool has_message() const          = 0;
    virtual bool receive(CommMessage &msg)    = 0;

    // peer management
    virtual bool add_peer(const CommMAC &addr) = 0;
    virtual bool remove_peer(const CommMAC &addr) = 0;
    virtual bool peer_exists(const CommMAC &addr) const = 0;

    // diagnostics
    virtual CommError last_error() const = 0;
    virtual CommStats stats() const      = 0;

    // control
    virtual void reset() = 0;

    virtual ~CommInterface() = default;
};

} // namespace comm
