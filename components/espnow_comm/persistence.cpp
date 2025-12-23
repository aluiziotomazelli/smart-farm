#include "persistence.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <string.h>

static const char *TAG = "PeerPersistence";

// Initialize RTC storage
PeerPersistence::RTCStorage PeerPersistence::rtc_storage = {0};

bool PeerPersistence::initNVS()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    return true;
}

std::vector<PeerPersistence::PersistentPeer> PeerPersistence::loadFromRTC()
{
    std::vector<PersistentPeer> peers;

    if (!isRTCDataValid()) {
        ESP_LOGD(TAG, "RTC data invalid or empty");
        return peers; // Empty
    }

    // Copy peers
    for (uint8_t i = 0; i < rtc_storage.count; i++) {
        peers.push_back(rtc_storage.peers[i]);
    }

    ESP_LOGI(TAG, "Loaded %zu peers from RTC", peers.size());
    return peers;
}

std::vector<PeerPersistence::PersistentPeer> PeerPersistence::loadFromNVS()
{
    std::vector<PersistentPeer> peers;

    nvs_handle_t handle;
    esp_err_t    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "No NVS namespace found");
        return peers;
    }

    // Get blob size
    size_t blob_size = 0;
    err              = nvs_get_blob(handle, NVS_KEY, NULL, &blob_size);
    if (err != ESP_OK || blob_size == 0) {
        nvs_close(handle);
        ESP_LOGD(TAG, "No peer data in NVS");
        return peers;
    }

    // Read blob
    std::vector<uint8_t> blob(blob_size);
    err = nvs_get_blob(handle, NVS_KEY, blob.data(), &blob_size);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read NVS blob");
        return peers;
    }

    // First byte is count
    if (blob_size < 1) {
        ESP_LOGE(TAG, "Invalid NVS data");
        return peers;
    }

    uint8_t count         = blob[0];
    size_t  expected_size = 1 + (count * sizeof(PersistentPeer));

    if (blob_size != expected_size) {
        ESP_LOGE(TAG, "NVS data size mismatch");
        return peers;
    }

    // Extract peers
    for (uint8_t i = 0; i < count; i++) {
        PersistentPeer peer;
        memcpy(&peer, &blob[1] + (i * sizeof(PersistentPeer)), sizeof(PersistentPeer));
        peers.push_back(peer);
    }

    ESP_LOGI(TAG, "Loaded %zu peers from NVS", peers.size());
    return peers;
}

bool PeerPersistence::saveToRTC(const std::vector<PersistentPeer> &peers)
{
    if (peers.size() > MAX_PEERS) {
        ESP_LOGE(TAG, "Too many peers for RTC: %zu", peers.size());
        return false;
    }

    // Clear and copy
    memset(&rtc_storage, 0, sizeof(rtc_storage));
    rtc_storage.count = static_cast<uint8_t>(peers.size());

    for (size_t i = 0; i < peers.size(); i++) {
        rtc_storage.peers[i] = peers[i];
    }

    // Calculate CRC
    rtc_storage.crc32 = calculateCRC32(&rtc_storage);

    ESP_LOGD(TAG, "Saved %zu peers to RTC", peers.size());
    return true;
}

bool PeerPersistence::saveToNVS(const std::vector<PersistentPeer> &peers)
{
    if (peers.size() > MAX_PEERS) {
        ESP_LOGE(TAG, "Too many peers for NVS: %zu", peers.size());
        return false;
    }

    nvs_handle_t handle;
    esp_err_t    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS");
        return false;
    }

    // Create blob: [count][peer1][peer2]...
    size_t               blob_size = 1 + (peers.size() * sizeof(PersistentPeer));
    std::vector<uint8_t> blob(blob_size);

    blob[0] = static_cast<uint8_t>(peers.size());
    for (size_t i = 0; i < peers.size(); i++) {
        memcpy(&blob[1] + (i * sizeof(PersistentPeer)), &peers[i],
               sizeof(PersistentPeer));
    }

    // Write to NVS
    err = nvs_set_blob(handle, NVS_KEY, blob.data(), blob_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write NVS blob");
        nvs_close(handle);
        return false;
    }

    // Commit
    err = nvs_commit(handle);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS");
        return false;
    }

    ESP_LOGD(TAG, "Saved %zu peers to NVS", peers.size());
    return true;
}

void PeerPersistence::clearAll()
{
    // Clear RTC
    memset(&rtc_storage, 0, sizeof(rtc_storage));

    // Clear NVS
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_key(handle, NVS_KEY);
        nvs_commit(handle);
        nvs_close(handle);
    }

    ESP_LOGI(TAG, "Cleared all persisted peers");
}

uint32_t PeerPersistence::calculateCRC32(const RTCStorage *storage)
{
    // Calculate CRC of everything except the crc32 field itself
    const uint8_t *data   = reinterpret_cast<const uint8_t *>(storage);
    size_t         length = sizeof(RTCStorage) - sizeof(uint32_t);

    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }

    return ~crc;
}

bool PeerPersistence::isRTCDataValid()
{
    if (rtc_storage.count == 0) {
        return false; // Empty
    }

    if (rtc_storage.count > MAX_PEERS) {
        return false; // Corrupted
    }

    // Check CRC
    uint32_t stored_crc     = rtc_storage.crc32;
    uint32_t calculated_crc = calculateCRC32(&rtc_storage);

    return stored_crc == calculated_crc;
}