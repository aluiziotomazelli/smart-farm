#include "comm_peer_manager.hpp"
#include "esp_timer.h"
#include <climits>

CommPeerManager &CommPeerManager::instance()
{
    static CommPeerManager inst;
    return inst;
}

int CommPeerManager::find_peer_index(uint32_t node_id) const
{
    for (int i = 0; i < COMM_MAX_PEERS; i++) {
        if (s_peers[i].valid && s_peers[i].node_id == node_id)
            return i;
    }
    return -1;
}

int CommPeerManager::find_free_or_oldest_slot() const
{
    int     oldest      = 0;
    int64_t oldest_time = INT64_MAX;
    for (int i = 0; i < COMM_MAX_PEERS; i++) {
        if (!s_peers[i].valid)
            return i;
        if (s_peers[i].last_seen_us < oldest_time) {
            oldest_time = s_peers[i].last_seen_us;
            oldest      = i;
        }
    }
    return oldest;
}

void CommPeerManager::upsert_peer(uint32_t node_id, const uint8_t mac[6])
{
    int     idx     = find_peer_index(node_id);
    int64_t now     = esp_timer_get_time();
    bool    changed = false;

    if (idx < 0) {
        idx     = find_free_or_oldest_slot();
        changed = true;
    }
    else if (memcmp(s_peers[idx].mac, mac, 6) != 0) {
        changed = true;
    }

    s_peers[idx].node_id = node_id;
    memcpy(s_peers[idx].mac, mac, 6);
    s_peers[idx].last_seen_us = now;
    s_peers[idx].valid        = true;

    if (s_nvs_backend && changed) {
        s_nvs_backend->save(s_nvs_ctx, node_id, mac);
    }
}

void CommPeerManager::init()
{
    memset(s_peers, 0, sizeof(s_peers));
}

bool CommPeerManager::resolve_peer(uint32_t node_id, uint8_t out_mac[6])
{
    int idx = find_peer_index(node_id);
    if (idx < 0)
        return false;

    memcpy(out_mac, s_peers[idx].mac, 6);
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
    for (int i = 0; i < COMM_MAX_PEERS; i++) {
        if (!s_peers[i].valid)
            continue;
        if ((now - s_peers[i].last_seen_us) > max_age_us)
            s_peers[i].valid = false;
    }
}

void CommPeerManager::set_nvs_backend(const comm_peer_nvs_if_t *backend, void *ctx)
{
    s_nvs_backend = backend;
    s_nvs_ctx     = ctx;

    if (!s_nvs_backend)
        return;

    uint32_t node_id;
    uint8_t  mac[6];
    bool     has_next = true;

    while (s_nvs_backend->load(s_nvs_ctx, &node_id, mac, &has_next) && has_next) {
        on_discovery_response(node_id, mac);
    }
}
