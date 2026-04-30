#pragma once

#include "farm_protocol_types.hpp"
#include <stdbool.h>
#include <stdint.h>

/* =========================
 *  Schema version
 * ========================= */
static constexpr uint32_t CORE_SCHEMA_VERSION = 1;

/* =========================
 *  Power profile
 * ========================= */
enum class PowerProfile : uint8_t
{
    ALWAYS_ON = 0,
    LOW_POWER,
    DEEP_SLEEP,
};

/* =========================
 *  Wakeup source
 * ========================= */
enum class WakeSource : uint8_t
{
    NONE = 0,
    TIMER,
    GPIO,
    POWER_ON,
};

// Flat struct for simplicity
struct CoreStorage
{
    // Schema
    uint32_t schema_version;

    // Identity (inline, no separate struct)
    FarmNodeId node_id;
    FarmNodeType node_type;
    uint8_t hw_revision;

    // Firmware
    uint8_t fw_major;
    uint8_t fw_minor;
    uint8_t fw_patch;

    // Lifecycle
    uint32_t boot_count;
    uint32_t crash_count;

    // Time
    bool has_valid_time;
    uint64_t unix_time;
    uint32_t last_sync_uptime;

    // Power
    PowerProfile power_profile;
    uint32_t sleep_interval_s;

    // Wake
    WakeSource last_wake;
};