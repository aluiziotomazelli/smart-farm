#pragma once

#include <cstdint>

namespace common
{
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
 * @brief Generates a unique 8-bit node ID from the device's base MAC address.
 *
 * This function uses a simple XOR combination of the last three bytes of the
 * factory MAC address to create a reasonably unique ID for the node.
 *
 * @return uint8_t The generated node ID.
 */
uint8_t generate_node_id();

} // namespace common