// comm_peer_manager.hpp
#pragma once
#include <stdbool.h>
#include <stdint.h>

class NvsCore; // forward declaration
class CommPeerManager
{
public:
    static CommPeerManager &instance()
    {
        static CommPeerManager inst;
        return inst;
    }

    // API pública
    void init();
    bool resolve_peer(uint32_t node_id, uint8_t out_mac[6]) const;
    void on_discovery_announce(uint32_t node_id, const uint8_t mac[6]);
    void on_discovery_response(uint32_t node_id, const uint8_t mac[6]);
    void on_packet_rx(uint32_t node_id, const uint8_t mac[6]);
    void purge(int64_t max_age_us);
    void attach_nvs(NvsCore *nvs);

    // NVS integration
    bool load(NvsCore *nvs); // load peers from NVS
    bool save(NvsCore *nvs); // save peers to NVS

    CommPeerManager()  = default; // singleton: ctor privado
    ~CommPeerManager() = default;

private:
    CommPeerManager(const CommPeerManager &)            = delete;
    CommPeerManager &operator=(const CommPeerManager &) = delete;

    // dados internos
    struct Peer
    {
        uint32_t node_id;
        uint8_t  mac[6];
        int64_t  last_seen_us;
        bool     valid;
    };

    static constexpr int MAX_PEERS = 16;
    Peer                 peers_[MAX_PEERS];

    NvsCore *_nvs;

    int  find_peer_index(uint32_t node_id) const;
    int  find_free_or_oldest_slot() const;
    void upsert_peer(uint32_t node_id, const uint8_t mac[6]);
};
