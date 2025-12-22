#pragma once
#include <cstdbool>
#include <cstdint>
#include <cstring>

#define COMM_MAX_PEERS 16

struct comm_peer_t
{
    uint32_t node_id;
    uint8_t  mac[6];
    int64_t  last_seen_us;
    bool     valid;
};

struct comm_peer_nvs_if_t
{
    bool (*load)(void *ctx, uint32_t *node_id, uint8_t mac[6], bool *has_next);
    bool (*save)(void *ctx, uint32_t node_id, const uint8_t mac[6]);
};

class CommPeerManager
{
public:
    // Singleton access
    static CommPeerManager &instance();

    void init();
    bool resolve_peer(uint32_t node_id, uint8_t out_mac[6]);
    void on_discovery_announce(uint32_t node_id, const uint8_t mac[6]);
    void on_discovery_response(uint32_t node_id, const uint8_t mac[6]);
    void on_packet_rx(uint32_t node_id, const uint8_t mac[6]);
    void purge(int64_t max_age_us);

    void set_nvs_backend(const comm_peer_nvs_if_t *backend, void *ctx);

private:
    CommPeerManager()                                   = default;
    ~CommPeerManager()                                  = default;
    CommPeerManager(const CommPeerManager &)            = delete;
    CommPeerManager &operator=(const CommPeerManager &) = delete;

    comm_peer_t s_peers[COMM_MAX_PEERS] = {};

    const comm_peer_nvs_if_t *s_nvs_backend = nullptr;
    void                     *s_nvs_ctx     = nullptr;

    int  find_peer_index(uint32_t node_id) const;
    int  find_free_or_oldest_slot() const;
    void upsert_peer(uint32_t node_id, const uint8_t mac[6]);
};
