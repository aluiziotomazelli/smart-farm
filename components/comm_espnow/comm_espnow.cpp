#include "comm_espnow.hpp"
#include "esp_now.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include <cstring>

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
    if (m_status != CommStatus::UNINITIALIZED) {
        return true;
    }

    esp_err_t err;

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        m_last_error = CommError::INIT_FAILED;
        m_status     = CommStatus::ERROR;
        return false;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
        m_last_error = CommError::INIT_FAILED;
        m_status     = CommStatus::ERROR;
        return false;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err                    = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        m_last_error = CommError::INIT_FAILED;
        m_status     = CommStatus::ERROR;
        return false;
    }

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    err = esp_now_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_init failed: %s", esp_err_to_name(err));
        m_last_error = CommError::INIT_FAILED;
        m_status     = CommStatus::ERROR;
        return false;
    }

    esp_now_register_send_cb(&CommEspNow::on_send_cb);
    esp_now_register_recv_cb(&CommEspNow::on_receive_cb);

    m_rx_queue = xQueueCreate(RX_QUEUE_LEN, sizeof(RxItem));
    if (!m_rx_queue) {
        ESP_LOGE(TAG, "xQueueCreate failed");
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
    if (m_status == CommStatus::UNINITIALIZED) {
        return;
    }

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
    // ESP-NOW is connectionless
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
        return false;
    }

    const size_t msg_len = ESP_NOW_MSG_HDR_LEN + msg.payload.size();
    if (msg_len > RX_MAX_PAYLOAD) {
        m_last_error = CommError::SEND_FAILED;
        return false;
    }

    std::vector<uint8_t> buffer(msg_len);
    EspNowMessage* esp_msg = reinterpret_cast<EspNowMessage*>(buffer.data());

    esp_msg->type = msg.type;
    std::memcpy(esp_msg->payload, msg.payload.data(), msg.payload.size());

    esp_err_t err = esp_now_send(msg.peer_addr.data(), buffer.data(), buffer.size());

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_now_send failed: %s", esp_err_to_name(err));
        m_last_error = CommError::SEND_FAILED;
        return false;
    }

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
    msg.peer_addr = item.peer_addr;
    msg.payload.assign(item.payload, item.payload + item.length);
    msg.flags   = 0;

    return true;
}

bool CommEspNow::add_peer(const CommMAC &addr)
{
    esp_now_peer_info_t peer{};
    std::memcpy(peer.peer_addr, addr.data(), addr.size());
    peer.encrypt = false; // TODO: Add support for encryption

    esp_err_t err = esp_now_add_peer(&peer);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
        ESP_LOGW(TAG, "esp_now_add_peer failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool CommEspNow::remove_peer(const CommMAC &addr)
{
    esp_err_t err = esp_now_del_peer(addr.data());
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_NOT_EXIST) {
        ESP_LOGW(TAG, "esp_now_del_peer failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool CommEspNow::peer_exists(const CommMAC &addr) const
{
    return esp_now_is_peer_exist(addr.data());
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

void CommEspNow::on_send_cb(const uint8_t *, esp_now_send_status_t status)
{
    if (!s_instance) return;

    if (status == ESP_NOW_SEND_SUCCESS) {
        s_instance->m_stats.tx_ok++;
    } else {
        s_instance->m_stats.tx_fail++;
    }
}

void CommEspNow::on_receive_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (!s_instance || !info || !data || len <= 0) return;

    CommMAC mac;
    std::memcpy(mac.data(), info->src_addr, mac.size());

    s_instance->enqueue_rx(mac, data, len);
}

bool CommEspNow::enqueue_rx(const CommMAC &addr, const uint8_t *data, size_t len)
{
    if (!m_rx_queue) return false;

    if (len < ESP_NOW_MSG_HDR_LEN || len > RX_MAX_PAYLOAD + ESP_NOW_MSG_HDR_LEN) {
        m_stats.rx_drop++;
        return false;
    }

    const EspNowMessage* esp_msg = reinterpret_cast<const EspNowMessage*>(data);
    const size_t payload_len = len - ESP_NOW_MSG_HDR_LEN;

    RxItem item{};
    item.type   = esp_msg->type;
    item.peer_addr = addr;
    item.length = payload_len;
    std::memcpy(item.payload, esp_msg->payload, payload_len);

    if (xQueueSend(m_rx_queue, &item, 0) != pdTRUE) {
        m_stats.rx_drop++;
        return false;
    }

    m_stats.rx_ok++;
    return true;
}
