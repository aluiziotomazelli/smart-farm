#pragma once

#include "comm_interface.hpp"

#include "esp_now.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <map>

namespace comm {

class CommEspNow final : public CommInterface
{
public:
    CommEspNow();
    ~CommEspNow() override;

    // CommInterface overrides
    bool init() override;
    bool start() override;
    void stop() override;

    bool is_ready() const override;
    bool is_connected() const override;
    CommStatus status() const override;

    bool send(const CommMessage &msg) override;
    bool has_message() const override;
    bool receive(CommMessage &msg) override;

    bool add_peer(uint32_t node_id) override;
    bool remove_peer(uint32_t node_id) override;
    bool peer_exists(uint32_t node_id) const override;

    CommError last_error() const override;
    CommStats stats() const override;

    void reset() override;

    // CommEspNow specific methods
    bool register_node(uint32_t node_id, const CommMAC& mac);


private:
    // ESP-NOW callbacks
    static void on_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status);
    static void on_receive_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len);

    // RX queue handling
    bool enqueue_rx(const CommMAC &addr, const uint8_t *data, size_t len);

private:
    // Internal message format to prepend type to the payload
    struct __attribute__((packed)) EspNowMessage
    {
        uint16_t type;
        uint8_t  payload[0];
    };

    static constexpr size_t RX_QUEUE_LEN      = 8;
    static constexpr size_t RX_MAX_PAYLOAD    = 250;
    static constexpr size_t ESP_NOW_MSG_HDR_LEN = sizeof(EspNowMessage);


    struct RxItem
    {
        uint32_t             src_node_id;
        uint16_t             type;
        size_t               length;
        uint8_t              payload[RX_MAX_PAYLOAD];
    };

    QueueHandle_t m_rx_queue = nullptr;
    CommStatus    m_status;
    CommError     m_last_error;
    CommStats     m_stats;

    std::map<uint32_t, CommMAC> m_node_id_to_mac;
    std::map<CommMAC, uint32_t> m_mac_to_node_id;
};

} // namespace comm
