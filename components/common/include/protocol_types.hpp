#pragma once
#include "esp_now.h"
#include <cstdint>
#include <cstddef> // Para size_t
constexpr size_t MESSAGE_HEADER_SIZE  = 12;
constexpr size_t CRC_SIZE             = 1;
// O payload maximo e o tamanho total menos o cabecalho e o CRC
constexpr size_t MAX_PAYLOAD_SIZE = ESP_NOW_MAX_DATA_LEN - MESSAGE_HEADER_SIZE - CRC_SIZE;

constexpr size_t MAX_PEERS = 20;

// Valores padrao (podem ser sobrescritos no config)
constexpr uint32_t DEFAULT_ACK_TIMEOUT_MS        = 500;
constexpr uint32_t DEFAULT_HEARTBEAT_INTERVAL_MS = 60000;
constexpr uint8_t DEFAULT_WIFI_CHANNEL           = 1;

enum class NodeType : uint8_t
{
    UNKNOWN = 0,
    HUB,
    WATER_TANK,
    SOLAR_SENSOR,
    LOAD_CONTROLLER,
    WEATHER,
};

enum class MessageType : uint8_t
{
    PAIR_REQUEST       = 0x00,
    PAIR_RESPONSE      = 0x01,
    HEARTBEAT          = 0x02,
    HEARTBEAT_RESPONSE = 0x03,
    DATA               = 0x10,
    ACK                = 0x11,
    COMMAND            = 0x20,
};

enum class PayloadType : uint8_t
{
    NONE                   = 0x00,
    WATER_LEVEL_REPORT     = 0x01,
    SOLAR_SENSOR_REPORT    = 0x02,
    WEATHER_REPORT         = 0x03,
    LOAD_CONTROLLER_STATUS = 0x04,
};

enum class PairStatus : uint8_t
{
    ACCEPTED             = 0x00,
    REJECTED_NOT_ALLOWED = 0x01,
    REJECTED_NO_SPACE    = 0x02,
};

enum class AckStatus : uint8_t
{
    OK                 = 0x00,
    ERROR_INVALID_DATA = 0x01,
    ERROR_PROCESSING   = 0x02,
};

enum class CommandType : uint8_t
{
    START_OTA           = 0x01,
    REBOOT              = 0x02,
    SET_REPORT_INTERVAL = 0x03,
};

enum class UsQuality : uint8_t
{
    OK,      /**< Measurement is reliable and within expected parameters. */
    WEAK,    /**< Measurement is valid but may have reduced accuracy. */
    INVALID, /**< Measurement is unreliable and should be discarded. */
};

enum class UsFailure : uint8_t
{
    NONE,          /**< No failure occurred. */
    TIMEOUT,       /**< The echo pulse was not received within the timeout period. */
    HW_ERROR,      /**< A hardware-level error, such as a stuck ECHO pin. */
    INVALID_PULSE, /**< The measured pulse corresponds to a distance outside the valid
                      range. */
    HIGH_VARIANCE, /**< The variance among valid pings is too high, indicating
                      instability. */
};
