#pragma once

#include <cstddef>
#include <cstdint>

#include "protocol_types.hpp"

namespace protocol {

/* =========================
 * Wire-level constants
 * ========================= */
constexpr uint8_t PROTOCOL_VERSION_WIRE = PROTOCOL_VERSION;
constexpr size_t  MAX_PAYLOAD_SIZE      = 192;              // safe for ESP-NOW
constexpr size_t  MAX_FRAME_SIZE        = sizeof(uint8_t) + // version
                                  sizeof(MessageType) + sizeof(uint32_t) + // flags
                                  sizeof(NodeId) + sizeof(NodeType) +
                                  sizeof(uint8_t) + // hw_revision
                                  MAX_PAYLOAD_SIZE;

/* =========================
 * Packed wire header
 * ========================= */
#pragma pack(push, 1)
struct WireHeader
{
    uint8_t     version;
    MessageType type;
    uint32_t    flags;

    NodeId   node_id;
    NodeType node_type;
    uint8_t  hw_revision;
};
#pragma pack(pop)

/* =========================
 * Runtime frame abstraction
 * ========================= */
struct Frame
{
    WireHeader header;

    uint8_t payload[protocol::MAX_PAYLOAD_SIZE]; //*payload; // non-owning
    size_t  payload_len;
};

/* =========================
 * Validation helpers
 * ========================= */
inline bool is_header_valid(const WireHeader &h)
{
    return h.version == PROTOCOL_VERSION_WIRE;
}

inline bool is_payload_size_valid(size_t len)
{
    return len <= MAX_PAYLOAD_SIZE;
}

bool   validate_frame(const WireHeader &header, size_t payload_len);
size_t frame_size(size_t payload_len);

} // namespace protocol
