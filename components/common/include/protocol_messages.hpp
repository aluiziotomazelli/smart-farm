#pragma once
#include "protocol_types.hpp"

#pragma pack(push, 1)

// ========== HEADER UNIVERSAL ==========
struct MessageHeader
{
    MessageType msg_type;
    uint16_t sequence_number;
    NodeType sender_type;
    uint8_t sender_id;
    PayloadType payload_type;
    bool requires_ack;
    uint8_t reserved;
    uint32_t timestamp_ms;
};

// ========== TRANSPORT LAYER ==========
struct PairRequest
{
    MessageHeader header;
    uint8_t firmware_version[3];
    uint32_t uptime_ms;
    char device_name[16];
};

struct PairResponse
{
    MessageHeader header;
    PairStatus status;
    uint8_t assigned_id;
    uint32_t heartbeat_interval_ms;
    uint32_t report_interval_ms;
    uint8_t wifi_channel;
};

struct HeartbeatMessage
{
    MessageHeader header;
    uint16_t battery_mv;
    int8_t rssi;
    uint32_t uptime_ms;
};

struct HeartbeatResponse
{
    MessageHeader header;
    uint32_t server_time_ms;
    uint8_t wifi_channel;
};

// ========== APPLICATION LAYER ==========
struct AckMessage
{
    MessageHeader header;
    uint16_t ack_sequence;
    AckStatus status;
    uint32_t processing_time_us;
};

struct WaterLevelReport
{
    MessageHeader header;
    uint16_t level_permille;
    float distance_cm;
    uint16_t battery_mv;
    UsQuality quality;
    UsFailure failure;
    bool float_switch_is_full;
    bool backup_mode_active;
};

struct SolarSensorReport
{
    MessageHeader header;
    uint16_t voltage_mv;
    uint16_t current_ma;
    uint16_t power_mw;
};

struct OtaCommand
{
    MessageHeader header;
    CommandType cmd_type;
    char firmware_url[128];
    uint32_t firmware_size;
    uint8_t firmware_hash[32];
};

#pragma pack(pop)

union PayloadUnion
{
    WaterLevelReport water_level_report;
    PairRequest pair_request;
    PairResponse pair_response;
    HeartbeatMessage heartbeat_message;
    HeartbeatResponse heartbeat_response;
    AckMessage ack_message;
    SolarSensorReport solar_sensor_report;
    OtaCommand ota_command;
}

// Validações de tamanho
static_assert(sizeof(MessageHeader) == MESSAGE_HEADER_SIZE, "Header size mismatch");
static_assert(sizeof(PayloadUnion) <= MAX_PAYLOAD_SIZE,
              "WaterLevelReport payload too large");