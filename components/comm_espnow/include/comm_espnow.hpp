#pragma once

#include "comm_interface.hpp"
#include "esp_now.h"
#include <cstdint>
#include <map>
#include <vector>
#include "freertos/FreeRTOS.h"

namespace comm {

class CommEspNow final : public CommInterface
{
public:
    static CommEspNow &instance();

    ~CommEspNow() override = default;

    CommEspNow(const CommEspNow &)            = delete;
    CommEspNow &operator=(const CommEspNow &) = delete;

    // Lifecycle
    bool init(uint32_t node_id) override;
    void stop() override;

    // Core send/receive
    bool send(uint32_t destination_node_id, const uint8_t *payload, size_t len) override;
    void set_rx_callback(RxCallback cb) override;

    static constexpr int MAX_PEERS = 16;
    struct Peer
    {
        uint32_t node_id;
        uint8_t  mac[6];
    };
    struct RtcData
    {
        uint32_t magic;
        uint8_t  num_peers;
        Peer     peers[MAX_PEERS];
        bool     nvs_dirty_flag;
    };
    static_assert(sizeof(RtcData) % 4 == 0, "RTC data size must be a multiple of 4");

private:
    CommEspNow() = default;

    static void on_receive_cb(const esp_now_recv_info_t *info,
                              const uint8_t             *data,
                              int                        len);
    static void on_send_cb(const esp_now_send_info_t *tx_info,
                           esp_now_send_status_t      status);

#pragma pack(push, 1)
    struct PacketHeader
    {
        uint32_t source_node_id;
        uint32_t destination_node_id;
    };
#pragma pack(pop)

    // static constexpr int MAX_PEERS = 16;
    // struct Peer
    // {
    //     uint32_t node_id;
    //     uint8_t  mac[6];
    // };

    // struct RtcData
    // {
    //     uint32_t magic;
    //     uint8_t  num_peers;
    //     Peer     peers[MAX_PEERS];
    //     bool     nvs_dirty_flag;
    // };
    // static_assert(sizeof(RtcData) % 4 == 0, "RTC data size must be a multiple of 4");
    // // static RTC_DATA_ATTR RtcData rtc_data_;

    std::map<uint32_t, std::vector<uint8_t>> m_peer_map;

    bool                         load_peers_from_nvs();
    bool                         save_peers_to_nvs();
    static constexpr const char *NVS_NAMESPACE = "comm_peers";

    RxCallback m_rx_callback;
    uint32_t   m_node_id        = 0;
    bool       m_is_initialized = false;

    void learn_peer(uint32_t node_id, const uint8_t mac[6]);
    bool find_peer_mac(uint32_t node_id, uint8_t out_mac[6]);
    void add_peer_to_espnow(const uint8_t mac[6]);

    static CommEspNow *s_instance; // For static callback context
};

} // namespace comm
