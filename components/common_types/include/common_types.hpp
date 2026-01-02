#pragma once

#include <cstdint>

enum class NodeType : uint8_t
{
    UNKNOWN = 0,
    WATER_TANK,
    SOLAR_SENSOR,
    LOAD_CONTROLLER,
    WEATHER,
    HUB,
};

/**
 * @brief Quality classification for an ultrasonic measurement.
 * @details Determines the reliability of a sensor reading.
 */
enum class UsQuality : uint8_t
{
    OK,      /**< Measurement is reliable and within expected parameters. */
    WEAK,    /**< Measurement is valid but may have reduced accuracy. */
    INVALID, /**< Measurement is unreliable and should be discarded. */
};

/**
 * @brief Defines the specific reason for a measurement failure.
 */
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

#pragma pack(push, 1)
struct WaterLevelReport
{
    uint16_t level_permille;
    float distance_cm;
    uint16_t battery_mv;
    UsQuality quality;
    UsFailure failure;
    bool float_switch_is_full;
    bool backup_mode_active;
};
#pragma pack(pop)
namespace common {
/**
 * @brief Generates a unique 8-bit node ID from the device's base MAC address.
 *
 * This function uses a simple XOR combination of the last three bytes of the
 * factory MAC address to create a reasonably unique ID for the node.
 *
 * @return uint8_t The generated node ID.
 */
uint8_t generate_node_id();

} // namespace common