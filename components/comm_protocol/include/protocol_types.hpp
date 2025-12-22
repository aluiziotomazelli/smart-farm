#pragma once

#include <cstdint>

#include "core_types.hpp" // do componente core

namespace protocol {

/* =========================
 * Protocol versioning
 * ========================= */
constexpr uint8_t PROTOCOL_VERSION = 1;

/* =========================
 * Node identification
 * ========================= */
using NodeId   = uint32_t;
using NodeType = ::NodeType;

/* =========================
 * Protocol message types
 * ========================= */
enum class MessageType : uint16_t
{
    INVALID = 0x0000,

    // discovery
    DISCOVERY_REQUEST  = 0x0001,
    DISCOVERY_RESPONSE = 0x0002,

    // generic status / telemetry
    STATUS = 0x0010,
    STATS  = 0x0011,

    // control / commands
    COMMAND = 0x0020,
    ACK     = 0x0021,

    // future extension
    USER_DEFINED_BASE = 0x8000
};

/* =========================
 * Protocol-level flags
 * ========================= */
enum MessageFlags : uint32_t
{
    FLAG_NONE      = 0x00000000,
    FLAG_BROADCAST = 0x00000001,
    FLAG_ACK_REQ   = 0x00000002,
    FLAG_ACK_RESP  = 0x00000004
};

/* =========================
 * Common protocol header
 * (logical, not packed)
 * ========================= */
struct ProtocolHeader
{
    uint8_t     version;
    MessageType type;
    uint32_t    flags;

    NodeId   node_id;
    NodeType node_type;
    uint8_t  hw_revision;
};

/* =========================
 * Discovery payloads
 * ========================= */
struct DiscoveryRequest
{
    // empty by design (broadcast ping)
};

struct DiscoveryResponse
{
    NodeId   node_id;
    uint32_t capabilities;
};

/* =========================
 * Peer information
 * ========================= */
struct PeerInfo
{
    NodeId   node_id;
    uint32_t capabilities;
    uint32_t last_seen_ms;
};

/* =========================
 * Capabilities bitmap
 * ========================= */
enum Capability : uint32_t
{
    CAP_NONE       = 0,
    CAP_WATER_TANK = 1 << 0,
    CAP_WEATHER    = 1 << 1,
    CAP_ACTUATOR   = 1 << 2,
};

} // namespace protocol
