#pragma once

// #include "esp_system.h"
// #include "nvs.h"
// #include "nvs_flash.h"
#include <array>
#include <cstdint>
#include <vector>

#ifdef CONFIG_ESP32_RTC_FAST_MEM
#define RTC_FAST_MEM_ATTR __attribute__((section(".rtc_fast")))
#else
#define RTC_FAST_MEM_ATTR
#endif

/**
 * @brief Simple persistent peer storage (MAC + node_id only)
 */
class PeerPersistence
{
public:
    // Estrutura minimalista: 6 bytes MAC + 1 byte node_id
    struct PersistentPeer
    {
        uint8_t mac[6];
        uint8_t node_id;
    } __attribute__((packed));

    static_assert(sizeof(PersistentPeer) == 7, "PersistentPeer deve ter 7 bytes");

    /**
     * @brief Initialize NVS once at startup
     */
    static bool initNVS();

    /**
     * @brief Load peers from RTC memory (fast, try this first)
     * @return Vector of peers, empty if no valid data
     */
    static std::vector<PersistentPeer> loadFromRTC();

    /**
     * @brief Load peers from NVS (slower, fallback)
     * @return Vector of peers, empty if no data
     */
    static std::vector<PersistentPeer> loadFromNVS();

    /**
     * @brief Save peers to RTC memory (fast, before deep sleep)
     * @param peers Vector of peers to save
     * @return true if successful
     */
    static bool saveToRTC(const std::vector<PersistentPeer> &peers);

    /**
     * @brief Save peers to NVS (slower, for long-term storage)
     * @param peers Vector of peers to save
     * @return true if successful
     */
    static bool saveToNVS(const std::vector<PersistentPeer> &peers);

    /**
     * @brief Clear all persisted data
     */
    static void clearAll();

private:
    // RTC memory storage structure
    struct RTCStorage
    {
        uint8_t        count;
        uint8_t        reserved[3]; // Align to 4 bytes
        PersistentPeer peers[20];   // Max 20 peers
        uint32_t       crc32;
    };

    static constexpr const char *NVS_NAMESPACE = "espnow_peers";
    static constexpr const char *NVS_KEY       = "peers";
    static constexpr size_t      MAX_PEERS     = 20;

    // RTC memory allocation
    RTC_FAST_MEM_ATTR static RTCStorage rtc_storage;

    // Helper methods
    static uint32_t calculateCRC32(const RTCStorage *storage);
    static bool     isRTCDataValid();
};