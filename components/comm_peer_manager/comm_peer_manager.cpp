#include "comm_peer_manager.hpp"
#include "nvs_core.hpp"

#include "esp_log.h"
#include "esp_timer.h"
#include <cstring>
#include <limits>

static const char *TAG = "CommPeerManager";
static constexpr char NVS_KEY_PEERS[] = "peer_store";

CommPeerManager::CommPeerManager()
{
    _mutex = xSemaphoreCreateMutex();
    init();
}

CommPeerManager::~CommPeerManager()
{
    vSemaphoreDelete(_mutex);
}

void CommPeerManager::init()
{
    xSemaphoreTake(_mutex, portMAX_DELAY);
    memset(peers_, 0, sizeof(peers_));
    xSemaphoreGive(_mutex);
}

void CommPeerManager::attach_nvs(NvsCore *nvs)
{
    _nvs = nvs;
}

bool CommPeerManager::load(NvsCore *nvs)
{
    if (!nvs)
        return false;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    struct Storage
    {
        Peer nvs_peers[MAX_PEERS];
    } storage;

    bool ok = nvs->loadStructPublic(NVS_KEY_PEERS, storage) == ESP_OK;
    if (ok)
    {
        memcpy(peers_, storage.nvs_peers, sizeof(peers_));
    }
    xSemaphoreGive(_mutex);
    return ok;
}

bool CommPeerManager::save(NvsCore *nvs)
{
    if (!nvs)
        return false;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    struct Storage
    {
        Peer nvs_peers[MAX_PEERS];
    } storage;

    memcpy(storage.nvs_peers, peers_, sizeof(peers_));
    bool ok = nvs->saveStructPublic(NVS_KEY_PEERS, storage) == ESP_OK;
    xSemaphoreGive(_mutex);
    return ok;
}

int CommPeerManager::find_peer_index_unsafe(uint32_t node_id) const
{
    for (int i = 0; i < MAX_PEERS; i++)
    {
        if (peers_[i].valid && peers_[i].node_id == node_id)
        {
            return i;
        }
    }
    return -1;
}

int CommPeerManager::find_free_or_oldest_slot_unsafe() const
{
    int oldest = 0;
    int64_t oldest_time = std::numeric_limits<int64_t>::max();

    for (int i = 0; i < MAX_PEERS; i++)
    {
        if (!peers_[i].valid)
            return i;
        if (peers_[i].last_seen_us < oldest_time)
        {
            oldest_time = peers_[i].last_seen_us;
            oldest = i;
        }
    }
    return oldest;
}

void CommPeerManager::upsert_peer_unsafe(uint32_t node_id, const uint8_t mac[6])
{
    int idx = find_peer_index_unsafe(node_id);
    int64_t now = esp_timer_get_time();

    if (idx < 0)
    {
        idx = find_free_or_oldest_slot_unsafe();
    }

    peers_[idx].node_id = node_id;
    memcpy(peers_[idx].mac, mac, 6);
    peers_[idx].last_seen_us = now;
    peers_[idx].valid = 1;
}

bool CommPeerManager::resolve_peer(uint32_t node_id, uint8_t out_mac[6])
{
    xSemaphoreTake(_mutex, portMAX_DELAY);
    int idx = find_peer_index_unsafe(node_id);
    if (idx >= 0)
    {
        memcpy(out_mac, peers_[idx].mac, 6);
    }
    xSemaphoreGive(_mutex);
    return idx >= 0;
}

void CommPeerManager::on_discovery_announce(uint32_t node_id, const uint8_t mac[6])
{
    xSemaphoreTake(_mutex, portMAX_DELAY);
    upsert_peer_unsafe(node_id, mac);
    xSemaphoreGive(_mutex);
}

void CommPeerManager::on_discovery_response(uint32_t node_id, const uint8_t mac[6])
{
    xSemaphoreTake(_mutex, portMAX_DELAY);
    upsert_peer_unsafe(node_id, mac);
    xSemaphoreGive(_mutex);
}

void CommPeerManager::on_packet_rx(uint32_t node_id, const uint8_t mac[6])
{
    xSemaphoreTake(_mutex, portMAX_DELAY);
    upsert_peer_unsafe(node_id, mac);
    xSemaphoreGive(_mutex);
}

void CommPeerManager::purge(int64_t max_age_us)
{
    xSemaphoreTake(_mutex, portMAX_DELAY);
    int64_t now = esp_timer_get_time();
    for (int i = 0; i < MAX_PEERS; i++)
    {
        if (!peers_[i].valid)
            continue;
        if ((now - peers_[i].last_seen_us) > max_age_us)
            peers_[i].valid = 0;
    }
    xSemaphoreGive(_mutex);
}
