#pragma once

#include "comm_interface.hpp"
#include "comm_peer_manager.hpp"
#include "protocol_frame.hpp"

#include "esp_now.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace comm {

class CommEspNow final : public CommInterface
{
public:
    static CommEspNow& instance();
    ~CommEspNow() override;

    // Lifecycle
    bool       init() override;
    bool       start() override;
    void       stop() override;
    CommStatus status() const override;
    void       reset() override;

    // Core communication
    bool send(const protocol::Frame &frame) override;
    bool has_message() const override;
    bool receive(protocol::Frame &out) override;

    // Peer management
    bool resolve_peer(uint32_t node_id, uint8_t out_mac[6]) override;
    void start_discovery(uint32_t target_node_id = 0xFFFFFFFF) override;
    void stop_discovery() override;

    // Persistence
    bool load() override;
    bool save() override;

private:
    CommEspNow();
    static void on_receive_cb(const esp_now_recv_info_t *info,
                              const uint8_t             *data,
                              int                        len);

    bool enqueue_rx(const uint8_t *data, size_t len, const uint8_t *src_mac);
    bool send_discovery_response(uint32_t target_node_id, const uint8_t target_mac[6]);
    static void peer_maintenance_task(void* arg);

private:
    static constexpr size_t RX_QUEUE_LEN   = 8;
    static constexpr size_t RX_MAX_PAYLOAD = 250;
    CommPeerManager         m_peer_mgr;

    struct RxItem
    {
        protocol::WireHeader header;
        uint16_t             payload_len;
        uint8_t              payload[protocol::MAX_PAYLOAD_SIZE];
        uint8_t              src_mac[6];
    };

    QueueHandle_t m_rx_queue = nullptr;
    TaskHandle_t m_maintenance_task_handle = nullptr;

    void handle_discovery_request(const protocol::Frame &frame, const uint8_t src_mac[6]);
    void handle_discovery_response(const protocol::Frame &frame,
                                   const uint8_t          src_mac[6]);

    uint32_t m_discovery_target = 0xFFFFFFFF; // 0xFFFFFFFF = broadcast
    bool     m_discovery_active = false;
};

} // namespace comm
