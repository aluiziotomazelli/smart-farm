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
 *  Estado OTA
 * ========================= */
enum class OtaState : uint8_t
{
    IDLE = 0,
    DOWNLOADING,
    APPLYING,
    VERIFYING,
    FAILED,
};

/* =========================
 *  Modo de operação
 * ========================= */
enum class LifecycleMode : uint8_t
{
    NORMAL = 0,
    SAFE,
    FACTORY,
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
    RADIO,
    POWER_ON,
    SOFTWARE,
};

/* =========================
 *  Hint para próximo wake
 * ========================= */
enum class WakeHint : uint8_t
{
    NONE = 0,
    CHECK_LEVEL,
    CHECK_CHARGING,
    PERIODIC_REPORT,
};

/* =========================
 *  Versão de firmware
 * ========================= */
struct FirmwareVersion
{
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
};

/* =========================
 *  Identidade persistente
 * ========================= */
struct NodeIdentity
{
    uint32_t node_id;
    NodeType type;
    uint8_t  hw_revision;
};

/* =========================
 *  Estado OTA persistente
 * ========================= */
struct OtaInfo
{
    OtaState state;
    uint8_t  fail_count;
    bool     last_boot_ok;
};

/* =========================
 *  Ciclo de vida do nó
 * ========================= */
struct LifecycleInfo
{
    LifecycleMode mode;
    uint32_t      boot_count;
    uint32_t      crash_count;
};

/* =========================
 *  Tempo / RTC
 * ========================= */
struct TimeInfo
{
    bool     has_valid_time;
    uint64_t unix_time;        // segundos
    uint32_t last_sync_uptime; // segundos desde boot
};

/* =========================
 *  Energia / Sleep
 * ========================= */
struct PowerInfo
{
    PowerProfile profile;
    uint32_t     sleep_interval_s;
};

/* =========================
 *  Wake info
 * ========================= */
struct WakeInfo
{
    WakeSource last_wake;
    WakeHint   next_wake_hint;
};

/* =========================
 *  Core persistido (blob NVS)
 * ========================= */
struct CoreStorage
{
    uint32_t schema_version;

    NodeIdentity    identity;
    FirmwareVersion fw_version;

    OtaInfo       ota;
    LifecycleInfo lifecycle;
    TimeInfo      time;
    PowerInfo     power;
    WakeInfo      wake;
};
