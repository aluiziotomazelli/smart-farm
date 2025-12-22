// comm_peer_manager.c

#include "comm_peer_manager.h"

#include "esp_timer.h"
#include <stdbool.h>
#include <string.h>

#define COMM_MAX_PEERS 16

typedef struct
{
    uint32_t node_id;
    uint8_t  mac[6];
    int64_t  last_seen_us;
    bool     valid;
} comm_peer_t;

static comm_peer_t s_peers[COMM_MAX_PEERS];

static int find_peer_index(uint32_t node_id)
{
    for (int i = 0; i < COMM_MAX_PEERS; i++) {
        if (s_peers[i].valid && s_peers[i].node_id == node_id) {
            return i;
        }
    }
    return -1;
}

static int find_free_or_oldest_slot(void)
{
    int     oldest      = 0;
    int64_t oldest_time = INT64_MAX;

    for (int i = 0; i < COMM_MAX_PEERS; i++) {
        if (!s_peers[i].valid) {
            return i;
        }
        if (s_peers[i].last_seen_us < oldest_time) {
            oldest_time = s_peers[i].last_seen_us;
            oldest      = i;
        }
    }
    return oldest;
}

static void upsert_peer(uint32_t node_id, const uint8_t mac[6])
{
    int     idx = find_peer_index(node_id);
    int64_t now = esp_timer_get_time();

    if (idx < 0) {
        idx = find_free_or_oldest_slot();
    }

    s_peers[idx].node_id = node_id;
    memcpy(s_peers[idx].mac, mac, 6);
    s_peers[idx].last_seen_us = now;
    s_peers[idx].valid        = true;
}

void comm_peer_manager_init(void)
{
    memset(s_peers, 0, sizeof(s_peers));
}

bool comm_peer_manager_resolve_peer(uint32_t node_id, uint8_t out_mac[6])
{
    int idx = find_peer_index(node_id);
    if (idx < 0) {
        return false;
    }

    memcpy(out_mac, s_peers[idx].mac, 6);
    return true;
}

void comm_peer_manager_on_discovery_announce(uint32_t node_id, const uint8_t mac[6])
{
    upsert_peer(node_id, mac);
}

void comm_peer_manager_on_discovery_response(uint32_t node_id, const uint8_t mac[6])
{
    upsert_peer(node_id, mac);
}

void comm_peer_manager_on_packet_rx(uint32_t node_id, const uint8_t mac[6])
{
    upsert_peer(node_id, mac);
}

void comm_peer_manager_purge(int64_t max_age_us)
{
    int64_t now = esp_timer_get_time();

    for (int i = 0; i < COMM_MAX_PEERS; i++) {
        if (!s_peers[i].valid) {
            continue;
        }

        if ((now - s_peers[i].last_seen_us) > max_age_us) {
            s_peers[i].valid = false;
        }
    }
}
