#pragma once

#include <array>
#include <cstdint>
#include <vector>

#ifdef CONFIG_ESP32_RTC_FAST_MEM
#define RTC_FAST_MEM_ATTR __attribute__((section(".rtc_fast")))
#else
#define RTC_FAST_MEM_ATTR
#endif

/**
 * @class PeerPersistence
 * @brief Handles the saving and loading of peer data to/from NVS and RTC memory.
 *
 * This is a static utility class that provides a simple persistence layer for
 * storing essential peer information (MAC address and node_id). It uses RTC
 * memory for fast recovery after deep sleep and NVS (flash) for long-term
 * storage across power cycles.
 */
class PeerPersistence
{
public:
    /**
     * @struct PersistentPeer
     * @brief A minimalist structure for storing peer information.
     */
    struct PersistentPeer
    {
        uint8_t mac[6];     ///< The MAC address of the peer.
        uint8_t node_id;    ///< The internal node ID of the peer.
    } __attribute__((packed));

    // Ensure the struct is exactly 7 bytes as expected.
    static_assert(sizeof(PersistentPeer) == 7, "PersistentPeer must be 7 bytes");

    /**
     * @brief Initializes the NVS flash storage.
     * @note This should be called once at application startup.
     * @return true on success.
     */
    static bool initNVS();

    /**
     * @brief Loads peers from RTC memory.
     * @return A vector of peers. The vector is empty if RTC data is invalid or not found.
     */
    static std::vector<PersistentPeer> loadFromRTC();

    /**
     * @brief Loads peers from NVS (flash memory).
     * @return A vector of peers. The vector is empty if no data is found.
     */
    static std::vector<PersistentPeer> loadFromNVS();

    /**
     * @brief Saves a list of peers to RTC memory.
     * @param peers A vector of peers to save.
     * @return true if the save operation was successful.
     */
    static bool saveToRTC(const std::vector<PersistentPeer> &peers);

    /**
     * @brief Saves a list of peers to NVS (flash memory).
     * @param peers A vector of peers to save.
     * @return true if the save operation was successful.
     */
    static bool saveToNVS(const std::vector<PersistentPeer> &peers);

    /**
     * @brief Clears all persisted peer data from both NVS and RTC memory.
     */
    static void clearAll();

private:
    /**
     * @struct RTCStorage
     * @brief Defines the data layout in RTC memory.
     */
    struct RTCStorage
    {
        uint8_t        count;                 ///< Number of peers currently stored.
        uint8_t        reserved[3];           ///< Padding to align the structure to 4 bytes.
        PersistentPeer peers[20];             ///< Array to store the peer data.
        uint32_t       crc32;                 ///< 32-bit CRC to validate the integrity of the data.
    };

    static constexpr const char *NVS_NAMESPACE = "espnow_peers";
    static constexpr const char *NVS_KEY       = "peers";
    static constexpr size_t      MAX_PEERS     = 20;

    // The actual storage block in RTC memory.
    RTC_FAST_MEM_ATTR static RTCStorage rtc_storage;

    // Helper methods
    static uint32_t calculateCRC32(const RTCStorage *storage);
    static bool     isRTCDataValid();
};
