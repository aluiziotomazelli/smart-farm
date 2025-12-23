#include "comm_espnow.hpp"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_rom_crc.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <cstring>
#include <vector>

namespace comm {

static const char   *TAG = "CommEspNow";
static RTC_DATA_ATTR CommEspNow::RtcData rtc_data_;
CommEspNow                              *CommEspNow::s_instance = nullptr;

CommEspNow &CommEspNow::instance()
{
    static CommEspNow s_instance_obj;
    return s_instance_obj;
}

bool CommEspNow::init(uint32_t node_id)
{
    if (m_is_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }

    m_node_id  = node_id;
    s_instance = this;

    // 1. Initialize Wi-Fi

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t          ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(ret));
        return false;
    }
    ret = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_storage failed: %s", esp_err_to_name(ret));
        return false;
    }
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(ret));
        return false;
    }
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(ret));
        return false;
    }

    // 2. Initialize ESP-NOW
    ret = esp_now_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_init failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_now_register_recv_cb(on_receive_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_register_recv_cb failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_now_register_send_cb(on_send_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_register_send_cb failed: %s", esp_err_to_name(ret));
        return false;
    }

    // 3. Load peer data (RTC or NVS)
    esp_reset_reason_t reason = esp_reset_reason();
    uint32_t           expected_magic =
        esp_rom_crc32_le(0, (uint8_t *)&m_node_id, sizeof(m_node_id));

    if (reason == ESP_RST_DEEPSLEEP && rtc_data_.magic == expected_magic) {
        ESP_LOGI(TAG, "Woke from deep sleep, loading peers from RTC");
        for (int i = 0; i < rtc_data_.num_peers; ++i) {
            m_peer_map[rtc_data_.peers[i].node_id] =
                std::vector<uint8_t>(rtc_data_.peers[i].mac, rtc_data_.peers[i].mac + 6);
        }
    }
    else {
        ESP_LOGI(TAG, "Power-on reset or invalid RTC data, loading peers from NVS");
        rtc_data_.magic          = expected_magic;
        rtc_data_.num_peers      = 0;
        rtc_data_.nvs_dirty_flag = false;
        load_peers_from_nvs(); // This will populate RTC data and m_peer_map
    }

    // 4. Add known peers to ESP-NOW
    for (const auto &pair : m_peer_map) {
        add_peer_to_espnow(pair.second.data());
    }

    m_is_initialized = true;
    ESP_LOGI(TAG, "CommEspNow initialized with node_id: %lu", m_node_id);
    return true;
}

void CommEspNow::stop()
{
    if (!m_is_initialized)
        return;

    if (rtc_data_.nvs_dirty_flag) {
        ESP_LOGI(TAG, "NVS is dirty, saving peers before stopping");
        save_peers_to_nvs();
    }

    esp_now_deinit();
    esp_wifi_stop();
    m_is_initialized = false;
    ESP_LOGI(TAG, "CommEspNow stopped");
}

void CommEspNow::set_rx_callback(RxCallback cb)
{
    m_rx_callback = cb;
}

bool CommEspNow::send(uint32_t destination_node_id, const uint8_t *payload, size_t len)
{
    if (!m_is_initialized) {
        ESP_LOGE(TAG, "Not initialized, cannot send");
        return false;
    }

    PacketHeader header = {.source_node_id      = m_node_id,
                           .destination_node_id = destination_node_id};

    std::vector<uint8_t> buffer(sizeof(header) + len);
    memcpy(buffer.data(), &header, sizeof(header));
    memcpy(buffer.data() + sizeof(header), payload, len);

    uint8_t  mac[6];
    uint8_t  broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t *target_mac       = broadcast_mac;

    if (find_peer_mac(destination_node_id, mac)) {
        target_mac = mac;
        ESP_LOGD(TAG, "Found peer %lu in map, sending unicast", destination_node_id);
    }
    else {
        ESP_LOGD(TAG, "Peer %lu not in map, sending broadcast", destination_node_id);
        add_peer_to_espnow(broadcast_mac);
    }

    esp_err_t ret = esp_now_send(target_mac, buffer.data(), buffer.size());
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_send failed: %s", esp_err_to_name(ret));
        return false;
    }

    return true;
}

void CommEspNow::on_receive_cb(const esp_now_recv_info_t *info,
                               const uint8_t             *data,
                               int                        len)
{
    if (s_instance == nullptr || data == nullptr || len < sizeof(PacketHeader)) {
        return;
    }

    const uint8_t      *mac_addr = info->src_addr;
    const PacketHeader *header   = reinterpret_cast<const PacketHeader *>(data);

    // Learn the sender's MAC/ID association
    s_instance->learn_peer(header->source_node_id, mac_addr);

    // Check if the packet is for us
    if (header->destination_node_id == s_instance->m_node_id ||
        header->destination_node_id == 0xFFFFFFFF) {
        if (s_instance->m_rx_callback) {
            const uint8_t *payload     = data + sizeof(PacketHeader);
            size_t         payload_len = len - sizeof(PacketHeader);
            s_instance->m_rx_callback(header->source_node_id, payload, payload_len);
        }
    }
}

void CommEspNow::on_send_cb(const esp_now_send_info_t *tx_info,
                            esp_now_send_status_t      status)
{
    if (tx_info == NULL) {
        ESP_LOGW(TAG, "Send cb tx_info error");
        return;
    }
    if (status == ESP_NOW_SEND_SUCCESS) {
        ESP_LOGD(TAG, "ESP_NOW_SEND_SUCCESS");
    }
    else {
        ESP_LOGW(TAG, "ESP_NOW_SEND_FAIL");
    }
}

void CommEspNow::learn_peer(uint32_t node_id, const uint8_t mac[6])
{
    std::vector<uint8_t> mac_vec(mac, mac + 6);
    auto                 it = m_peer_map.find(node_id);

    if (it == m_peer_map.end() || it->second != mac_vec) {
        ESP_LOGI(TAG, "Learned new/updated peer: id=%lu, mac=" MACSTR, node_id,
                 MAC2STR(mac));

        m_peer_map[node_id] = mac_vec;

        // Update RTC data
        int peer_idx = -1;
        for (int i = 0; i < rtc_data_.num_peers; ++i) {
            if (rtc_data_.peers[i].node_id == node_id) {
                peer_idx = i;
                break;
            }
        }

        if (peer_idx == -1 && rtc_data_.num_peers < MAX_PEERS) {
            peer_idx = rtc_data_.num_peers++;
        }

        if (peer_idx != -1) {
            rtc_data_.peers[peer_idx].node_id = node_id;
            memcpy(rtc_data_.peers[peer_idx].mac, mac, 6);
            rtc_data_.nvs_dirty_flag = true;
        }
        else {
            ESP_LOGW(TAG, "Peer cache is full, cannot learn new peer");
        }

        add_peer_to_espnow(mac);
    }
}

bool CommEspNow::find_peer_mac(uint32_t node_id, uint8_t out_mac[6])
{
    auto it = m_peer_map.find(node_id);
    if (it != m_peer_map.end()) {
        memcpy(out_mac, it->second.data(), 6);
        return true;
    }
    return false;
}

void CommEspNow::add_peer_to_espnow(const uint8_t mac[6])
{
    if (esp_now_is_peer_exist(mac)) {
        return;
    }

    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, mac, 6);
    peer_info.channel = 0; // 0 means current channel
    peer_info.encrypt = false;

    esp_err_t ret = esp_now_add_peer(&peer_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_add_peer failed: %s", esp_err_to_name(ret));
    }
}

bool CommEspNow::load_peers_from_nvs()
{
    nvs_handle_t nvs_handle;
    esp_err_t    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(ret));
        return false;
    }

    size_t required_size = 0;
    ret                  = nvs_get_blob(nvs_handle, "peers", nullptr, &required_size);
    if (ret == ESP_OK && required_size > 0) {
        std::vector<uint8_t> buffer(required_size);
        ret = nvs_get_blob(nvs_handle, "peers", buffer.data(), &required_size);
        if (ret == ESP_OK) {
            // Reconstruct RTC data from buffer
            if (required_size <= sizeof(rtc_data_.peers)) {
                rtc_data_.num_peers = required_size / sizeof(Peer);
                memcpy(rtc_data_.peers, buffer.data(), required_size);
                // Also populate the RAM map
                m_peer_map.clear();
                for (int i = 0; i < rtc_data_.num_peers; ++i) {
                    m_peer_map[rtc_data_.peers[i].node_id] = std::vector<uint8_t>(
                        rtc_data_.peers[i].mac, rtc_data_.peers[i].mac + 6);
                }
                ESP_LOGI(TAG, "Loaded %d peers from NVS", rtc_data_.num_peers);
            }
        }
    }
    nvs_close(nvs_handle);
    return ret == ESP_OK || ret == ESP_ERR_NVS_NOT_FOUND;
}

bool CommEspNow::save_peers_to_nvs()
{
    nvs_handle_t nvs_handle;
    esp_err_t    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = nvs_set_blob(nvs_handle, "peers", rtc_data_.peers,
                       rtc_data_.num_peers * sizeof(Peer));
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs_handle);
        if (ret == ESP_OK) {
            rtc_data_.nvs_dirty_flag = false;
            ESP_LOGI(TAG, "Saved %d peers to NVS", rtc_data_.num_peers);
        }
        else {
            ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(ret));
        }
    }
    else {
        ESP_LOGE(TAG, "nvs_set_blob failed: %s", esp_err_to_name(ret));
    }
    nvs_close(nvs_handle);
    return ret == ESP_OK;
}

} // namespace comm
