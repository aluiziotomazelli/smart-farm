#pragma once

#include "comm_interface.hpp"

#include "esp_now.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace comm {

class CommEspNow final : public CommInterface
{
public:
    CommEspNow();
    ~CommEspNow() override;

    bool init() override;
    bool start() override;
    void stop() override;

    bool       is_ready() const override;
    bool       is_connected() const override;
    CommStatus status() const override;

    bool send(const CommMessage &msg) override;
    bool has_message() const override;
    bool receive(CommMessage &msg) override;

    CommError last_error() const override;
    CommStats stats() const override;

    void reset() override;

private:
    static void on_receive_cb(const esp_now_recv_info_t *info,
                              const uint8_t             *data,
                              int                        len);

    bool enqueue_rx(const uint8_t *data, size_t len);

private:
    static constexpr size_t RX_QUEUE_LEN   = 8;
    static constexpr size_t RX_MAX_PAYLOAD = 250;

    struct RxItem
    {
        uint16_t type;
        size_t   length;
        uint8_t  payload[RX_MAX_PAYLOAD];
    };

    QueueHandle_t m_rx_queue = nullptr;

    CommStatus m_status;
    CommError  m_last_error;
    CommStats  m_stats;
};

} // namespace comm