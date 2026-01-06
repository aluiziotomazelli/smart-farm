#pragma once

#include "esp_now.h"
#include "message_types.hpp" // For MessageType enum and OtaCommand struct
#include <cstdint>

namespace EspNowQueue
{

/**
 * @brief Defines the type of event being sent through the queue.
 *
 * Prefixes indicate the direction of the event:
 *   - CMD: From the application/public API to the EspNowComm task (a command to execute).
 *   - EVT: From internal ESP-NOW callbacks to the EspNowComm task (an event that occurred).
 */
enum class EventType : uint8_t
{
    CMD_SEND_PACKET,
    CMD_ADD_PEER,
    CMD_REMOVE_PEER,
    CMD_START_DISCOVERY,
    CMD_STOP_DISCOVERY,
    EVT_PACKET_RECEIVED,
    EVT_SEND_STATUS,
    CMD_TASK_TERMINATE,
};

// --- Command Payloads (Application -> Task) ---

/**
 * @brief Payload for CMD_SEND_PACKET.
 *
 * This struct is used to request the sending of any type of message.
 * The public API function will be responsible for packing application data
 * (e.g., WaterLevelReport, OtaCommand) into the payload buffer.
 */
struct CommandSendPacket
{
    uint8_t node_id;         // Destination Node ID. Use 0xFF for broadcast.
    bool require_ack;
    MessageType message_type;  // Differentiates between DATA, OTA, etc.
    uint16_t payload_len;
    // The raw data to be sent.
    static constexpr size_t MAX_PAYLOAD_SIZE =
        ESP_NOW_MAX_DATA_LEN - sizeof(DataHeader) - 1;
    uint8_t payload[MAX_PAYLOAD_SIZE];
};

/**
 * @brief Payload for CMD_ADD_PEER.
 */
struct CommandAddPeer
{
    uint8_t node_id;
    uint8_t mac_addr[6];
    uint8_t channel;
    bool    encrypt;
};

// --- Event Payloads (ESP-NOW Callbacks -> Task) ---

/**
 * @brief Payload for EVT_PACKET_RECEIVED.
 */
struct EventPacketReceived
{
    esp_now_recv_info_t recv_info; // Contains source MAC, RSSI etc.
    uint8_t data[ESP_NOW_MAX_DATA_LEN];
    int len;
};

/**
 * @brief Payload for EVT_SEND_STATUS.
 */
struct EventSendStatus
{
    esp_now_send_info_t tx_info;
    esp_now_send_status_t status;
};

/**
 * @brief The main event structure that is passed through the FreeRTOS queue.
 *
 * It contains the event type and a union of all possible payload structures.
 */
struct Event
{
    EventType type;

    union
    {
        // Payloads for commands
        CommandSendPacket cmd_send_packet;
        CommandAddPeer    cmd_add_peer;
        uint8_t           cmd_remove_peer_node_id;
        uint32_t          cmd_start_discovery_timeout_ms;

        // Payloads for internal events
        EventPacketReceived evt_packet_received;
        EventSendStatus   evt_send_status;
    } data;
};

} // namespace EspNowQueue
