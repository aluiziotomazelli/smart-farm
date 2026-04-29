#pragma once

#include <cstdint>

using NodeId = uint8_t;
using NodeType = uint8_t;
using PayloadType = uint8_t;

constexpr size_t APP_MAX_PAYLOAD_SIZE = 230;

/**
 * @brief Application-specific Node IDs for the Farm project.
 * These are mapped to the generic NodeId (uint8_t).
 */
enum class FarmNodeId : NodeId
{
    UNKNOWN      = 0x00,
    HUB          = 0x01, // Reserved: Central Hub
    WATER_TANK   = 0x05,
    SOLAR_SENSOR = 0x07,
    PUMP_CONTROL = 0x0A,
    WEATHER      = 0x0C,
    BROADCAST    = 0xFF, // Reserved: Broadcast
};

/**
 * @brief Application-specific Node Types for the Farm project.
 */
enum class FarmNodeType : NodeType
{
    UNKNOWN  = 0x00,
    HUB      = 0x01, // Reserved: Central Hub
    SENSOR   = 0x02,
    ACTUATOR = 0x03,
};

/**
 * @brief Application-specific Payload Types for the Farm project.
 */
enum class FarmPayloadType : PayloadType
{
    WATER_LEVEL_REPORT = 0x01,
    SOLAR_SENSOR_REPORT = 0x02,
    WEATHER_REPORT = 0x03,
    LOAD_CONTROLLER_STATUS = 0x04,
};

#pragma pack(push, 1)

struct WaterLevelReport
{
    uint16_t level_permille;
    float distance_cm;
    uint16_t battery_mv;
    uint8_t quality; // From ultrasonic_sensor UsResult or similar
    uint8_t failure;
    bool float_switch_is_full;
    bool backup_mode_active;
};

struct SolarSensorReport
{
    uint16_t voltage_mv;
    uint16_t current_ma;
    uint16_t power_mw;
};

#pragma pack(pop)

// Validations to ensure that no payload exceeds the ESP-NOW payload limits
static_assert(sizeof(WaterLevelReport) <= APP_MAX_PAYLOAD_SIZE, "WaterLevelReport payload is too large");
static_assert(sizeof(SolarSensorReport) <= APP_MAX_PAYLOAD_SIZE, "SolarSensorReport payload is too large");
