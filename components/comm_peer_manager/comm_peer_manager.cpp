#include "comm_peer_manager.hpp"
#include "nvs_core.hpp"

#include "esp_log.h"
#include "esp_timer.h"
#include <cstring>
#include <limits>

static const char    *TAG             = "CommPeerManager";
static constexpr char NVS_KEY_PEERS[] = "peer_store";

// ----------------------------
// PeerStorage interno para NVS
// ----------------------------

// ----------------------------
// Attach NVS handler
// ----------------------------
void CommPeerManager::attach_nvs(NvsCore *nvs)
{
    _nvs = nvs;
}

// ----------------------------
// Load peers from NVS
// ----------------------------
bool CommPeerManager::load(NvsCore *nvs)
{
    if (!nvs)
        return false;

    struct Storage
    {
        Peer nvs_peers[MAX_PEERS];
    } storage;

    if (nvs->loadStructPublic(NVS_KEY_PEERS, storage) != ESP_OK)
        return false;

    memcpy(peers_, storage.nvs_peers, sizeof(peers_));
    return true;
}

// ----------------------------
// Save peers to NVS
// ----------------------------
bool CommPeerManager::save(NvsCore *nvs)
{
    if (!nvs)
        return false;

    struct Storage
    {
        Peer nvs_peers[MAX_PEERS];
    } storage;

    memcpy(storage.nvs_peers, peers_, sizeof(peers_));

    return nvs->saveStructPublic(NVS_KEY_PEERS, storage) == ESP_OK;
}

// ----------------------------
// Peer lookup
// ----------------------------
int CommPeerManager::find_peer_index(uint32_t node_id) const
{
    for (int i = 0; i < MAX_PEERS; i++) {
        if (peers_[i].valid && peers_[i].node_id == node_id) {
            return i;
        }
    }
    return -1;
}

int CommPeerManager::find_free_or_oldest_slot() const
{
    int     oldest      = 0;
    int64_t oldest_time = std::numeric_limits<int64_t>::max();

    for (int i = 0; i < MAX_PEERS; i++) {
        if (!peers_[i].valid)
            return i;
        if (peers_[i].last_seen_us < oldest_time) {
            oldest_time = peers_[i].last_seen_us;
            oldest      = i;
        }
    }
    return oldest;
}

// ----------------------------
// Insert or update peer
// ----------------------------
void CommPeerManager::upsert_peer(uint32_t node_id, const uint8_t mac[6])
{
    int     idx = find_peer_index(node_id);
    int64_t now = esp_timer_get_time();

    if (idx < 0) {
        idx = find_free_or_oldest_slot();
    }

    peers_[idx].node_id = node_id;
    memcpy(peers_[idx].mac, mac, 6);
    peers_[idx].last_seen_us = now;
    peers_[idx].valid        = true;
}

// ----------------------------
// Public API
// ----------------------------
bool CommPeerManager::resolve_peer(uint32_t node_id, uint8_t out_mac[6]) const
{
    int idx = find_peer_index(node_id);
    if (idx < 0)
        return false;

    memcpy(out_mac, peers_[idx].mac, 6);
    return true;
}

void CommPeerManager::on_discovery_announce(uint32_t node_id, const uint8_t mac[6])
{
    upsert_peer(node_id, mac);
}

void CommPeerManager::on_discovery_response(uint32_t node_id, const uint8_t mac[6])
{
    upsert_peer(node_id, mac);
}

void CommPeerManager::on_packet_rx(uint32_t node_id, const uint8_t mac[6])
{
    upsert_peer(node_id, mac);
}

void CommPeerManager::purge(int64_t max_age_us)
{
    int64_t now = esp_timer_get_time();
    for (int i = 0; i < MAX_PEERS; i++) {
        if (!peers_[i].valid)
            continue;
        if ((now - peers_[i].last_seen_us) > max_age_us)
            peers_[i].valid = false;
    }
}
