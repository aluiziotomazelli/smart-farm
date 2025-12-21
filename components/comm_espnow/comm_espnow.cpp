#include "comm_espnow.hpp"

#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"

static const char *TAG = "CommEspNow";

using namespace comm;

static CommEspNow *s_instance = nullptr;

CommEspNow::CommEspNow()
{
    m_status     = CommStatus::UNINITIALIZED;
    m_last_error = CommError::NONE;
}

CommEspNow::~CommEspNow()
{
    stop();
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

bool CommEspNow::send(const CommMessage &msg)
{
    if (m_status != CommStatus::RUNNING) {
        m_last_error = CommError::NOT_READY;
        m_stats.tx_fail++;
        return false;
    }

    esp_err_t err = esp_now_send(nullptr, // broadcast
                                 static_cast<const uint8_t *>(msg.payload), msg.length);

    if (err != ESP_OK) {
        m_last_error = CommError::SEND_FAILED;
        m_stats.tx_fail++;
        return false;
    }

    m_stats.tx_ok++;
    return true;
}

bool CommEspNow::has_message() const
{
    return m_rx_queue && uxQueueMessagesWaiting(m_rx_queue) > 0;
}

bool CommEspNow::receive(CommMessage &msg)
{
    if (!m_rx_queue)
        return false;

    RxItem item;
    if (xQueueReceive(m_rx_queue, &item, 0) != pdTRUE) {
        return false;
    }

    msg.type    = item.type;
    msg.payload = item.payload;
    msg.length  = item.length;
    msg.flags   = 0;

    m_stats.rx_ok++;
    return true;
}

CommError CommEspNow::last_error() const
{
    return m_last_error;
}

CommStats CommEspNow::stats() const
{
    return m_stats;
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
    if (!m_rx_queue)
        return false;

    if (len > RX_MAX_PAYLOAD) {
        m_stats.rx_drop++;
        return false;
    }

    RxItem item{};
    item.type   = 0;
    item.length = len;
    memcpy(item.payload, data, len);

    if (xQueueSend(m_rx_queue, &item, 0) != pdTRUE) {
        m_stats.rx_drop++;
        return false;
    }

    return true;
}
