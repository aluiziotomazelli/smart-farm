#include "comm_espnow.hpp"
#include "protocol_codec.hpp"

#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"

static constexpr uint8_t ESPNOW_BROADCAST_ADDR[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF,
                                                                    0xFF, 0xFF, 0xFF};

static const char *TAG = "CommEspNow";

using namespace comm;

static CommEspNow *s_instance = nullptr;

CommEspNow::CommEspNow()
{
    if (s_instance) {
        ESP_LOGE(TAG, "CommEspNow already instantiated. This is a singleton-like class.");
    }
    m_status     = CommStatus::UNINITIALIZED;
    m_last_error = CommError::NONE;
    s_instance   = this;
}

CommEspNow::~CommEspNow()
{
    stop();
    s_instance = nullptr;
}

bool CommEspNow::init()
{
    esp_err_t err;

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        m_last_error = CommError::INIT_FAILED;
        m_status     = CommStatus::ERROR;
        return false;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        m_last_error = CommError::INIT_FAILED;
        m_status     = CommStatus::ERROR;
        return false;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err                    = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        m_last_error = CommError::INIT_FAILED;
        m_status     = CommStatus::ERROR;
        return false;
    }

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    err = esp_now_init();
    if (err != ESP_OK) {
        m_last_error = CommError::INIT_FAILED;
        m_status     = CommStatus::ERROR;
        return false;
    }

    esp_now_register_recv_cb(&CommEspNow::on_receive_cb);

    m_rx_queue = xQueueCreate(RX_QUEUE_LEN, sizeof(RxItem));
    if (!m_rx_queue) {
        m_status     = CommStatus::ERROR;
        m_last_error = CommError::INTERNAL_ERROR;
        return false;
    }

    m_status     = CommStatus::READY;
    m_last_error = CommError::NONE;
    return true;
}

bool CommEspNow::start()
{
    if (m_status != CommStatus::READY) {
        m_last_error = CommError::NOT_READY;
        return false;
    }

    m_status = CommStatus::RUNNING;
    return true;
}

void CommEspNow::stop()
{
    if (m_rx_queue) {
        vQueueDelete(m_rx_queue);
        m_rx_queue = nullptr;
    }

    esp_now_deinit();
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_event_loop_delete_default();
    esp_netif_deinit();
    m_status = CommStatus::UNINITIALIZED;
}

bool CommEspNow::is_ready() const
{
    return m_status == CommStatus::READY || m_status == CommStatus::RUNNING;
}

bool CommEspNow::is_connected() const
{
    // ESP-NOW não tem conceito real de conexão
    return m_status == CommStatus::RUNNING;
}

CommStatus CommEspNow::status() const
{
    return m_status;
}

bool CommEspNow::send(const protocol::Frame &frame)
{
    if (m_status != CommStatus::RUNNING) {
        m_last_error = CommError::NOT_READY;
        return false;
    }

    uint8_t buffer[protocol::MAX_FRAME_SIZE];
    size_t  written = 0;

    auto res = protocol::encode_frame(frame, buffer, sizeof(buffer), written);

    if (res != protocol::CodecResult::OK)
        return false;

    const uint8_t *peer = (frame.header.flags & protocol::FLAG_BROADCAST)
                              ? ESPNOW_BROADCAST_ADDR
                              : resolve_peer(frame.header.node_id); // stub por enquanto

    esp_err_t err = esp_now_send(peer, buffer, written);

    if (err != ESP_OK) {
        m_last_error = CommError::SEND_FAILED;
        return false;
    }

    return true;
}

bool CommEspNow::has_message() const
{
    return m_rx_queue && uxQueueMessagesWaiting(m_rx_queue) > 0;
}

bool CommEspNow::receive(protocol::Frame &out)
{
    if (!m_rx_queue)
        return false;

    RxItem item;

    if (xQueueReceive(m_rx_queue, &item, 0) != pdTRUE)
        return false;

    out.header      = item.header;
    out.payload     = item.payload;
    out.payload_len = item.payload_len;

    return true;
}

CommError CommEspNow::last_error() const
{
    return m_last_error;
}

void CommEspNow::reset()
{
    stop();
    init();
}

void CommEspNow::on_receive_cb(const esp_now_recv_info_t *, const uint8_t *data, int len)
{
    if (!s_instance)
        return;
    s_instance->enqueue_rx(data, len);
}

bool CommEspNow::enqueue_rx(const uint8_t *data, size_t len)
{
    if (!m_rx_queue || len > protocol::MAX_FRAME_SIZE) {
        return false;
    }

    RxItem item{};

    item.payload_len = len - sizeof(protocol::WireHeader);
    if (item.payload_len > protocol::MAX_PAYLOAD_SIZE)
        return false;

    memcpy(&item.header, data, sizeof(protocol::WireHeader));

    if (item.payload_len > 0) {
        memcpy(item.payload, data + sizeof(protocol::WireHeader), item.payload_len);
    }

    return xQueueSend(m_rx_queue, &item, 0) == pdTRUE;
}
