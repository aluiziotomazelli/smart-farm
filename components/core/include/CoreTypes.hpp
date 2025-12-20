#pragma once

#include <stdbool.h>
#include <stdint.h>

/* =========================
 *  Versão do schema
 * ========================= */
static constexpr uint32_t CORE_SCHEMA_VERSION = 1;

/* =========================
 *  Identidade do nó
 * ========================= */
enum class NodeType : uint8_t
{
    UNKNOWN = 0,
    WATER_TANK,
    SOLAR_SENSOR,
    LOAD_CONTROLLER,
    WEATHER,
    HUB,
};

/* =========================
 *  Perfil de energia
 * ========================= */
enum class PowerProfile : uint8_t
{
    ALWAYS_ON = 0,
    LOW_POWER,
    DEEP_SLEEP,
};

/* =========================
 *  Origem do wakeup
 * ========================= */
enum class WakeSource : uint8_t
{
    NONE = 0,
    TIMER,
    GPIO,
    POWER_ON,
};

// Struct flat - mais simples!
struct CoreStorage
{
    // Schema
    uint32_t schema_version;

    // Identity (inline, sem struct separada)
    uint32_t node_id;
    NodeType node_type;
    uint8_t  hw_revision;

    // Firmware
    uint8_t fw_major;
    uint8_t fw_minor;
    uint8_t fw_patch;

    // Lifecycle
    uint32_t boot_count;
    uint32_t crash_count;

    // Time
    bool     has_valid_time;
    uint64_t unix_time;
    uint32_t last_sync_uptime;

    // Power
    PowerProfile power_profile;
    uint32_t     sleep_interval_s;

    // Wake
    WakeSource last_wake;
};