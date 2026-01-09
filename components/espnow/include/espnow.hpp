#pragma once

#include "esp_now.h"
#include "protocol_types.hpp"
#include <cstdint>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// Configuration to initialize the EspNow component
struct EspNowConfig
{
    NodeId node_id;
    NodeType node_type;
    QueueHandle_t app_rx_queue;
    uint8_t wifi_channel;
    uint32_t ack_timeout_ms;
    uint32_t heartbeat_interval_ms;
    bool is_master;

    // Default constructor
    EspNowConfig()
        : node_id(NodeId::HUB) // Default to HUB, should be overridden by the application
        , node_type(NodeType::UNKNOWN)
        , app_rx_queue(nullptr)
        , wifi_channel(DEFAULT_WIFI_CHANNEL)
        , ack_timeout_ms(DEFAULT_ACK_TIMEOUT_MS)
        , heartbeat_interval_ms(DEFAULT_HEARTBEAT_INTERVAL_MS)
        , is_master(false)
    {
    }
};

// Main class for ESP-NOW communication.
// Manages pairing, heartbeats, and asynchronous message exchange.
class EspNow
{
public:
    // Singleton
    static EspNow &instance();

    // Deleted copy constructor and assignment operator
    EspNow(const EspNow &)            = delete;
    EspNow &operator=(const EspNow &) = delete;

    // Destructor to free resources
    ~EspNow();

    // Generic structure for received packets used in queues
    struct RxPacket
    {
        uint8_t src_mac[6];
        uint8_t data[ESP_NOW_MAX_DATA_LEN];
        size_t len;
        int8_t rssi;
        int64_t timestamp_us;
    };

    // Public information about a peer, safe to be used by the application
    struct PeerInfo
    {
        uint8_t mac[6];
        NodeType type;
        NodeId node_id;
        uint8_t channel;
        uint32_t last_seen_ms;
        bool paired;
    };

    // Public API
    static constexpr int MAX_PEERS = 19; // Reserve one slot for the broadcast address

    esp_err_t init(const EspNowConfig &config);
    esp_err_t send(NodeId dest_node_id,
                   PayloadType payload_type,
                   const void *payload,
                   size_t len,
                   bool require_ack = false);

    // Peer Management Functions
    esp_err_t add_peer(NodeId node_id,
                       const uint8_t *mac,
                       uint8_t channel,
                       NodeType type);
    esp_err_t remove_peer(NodeId node_id);
    std::vector<PeerInfo> get_peers();
    esp_err_t start_pairing(uint32_t timeout_ms = 30000);

private:
    // Private constructor for singleton pattern
    EspNow();

    // --- Private Members ---
    EspNowConfig config_{};
    std::vector<PeerInfo> peers_;
    SemaphoreHandle_t peers_mutex_ = nullptr;
    bool is_initialized_           = false;
    bool is_pairing_active_        = false;
    TimerHandle_t pairing_timer_handle_ = nullptr;
    TimerHandle_t pairing_timeout_timer_handle_ = nullptr;

    // Queues for the Dispatcher-Worker architecture
    QueueHandle_t rx_dispatch_queue_      = nullptr; // Single queue for the ISR
    QueueHandle_t transport_worker_queue_ = nullptr; // Queue for the protocol task

    // Task handles
    TaskHandle_t rx_dispatch_task_handle_      = nullptr;
    TaskHandle_t transport_worker_task_handle_ = nullptr;

    // Singleton instance and mutex
    static EspNow *instance_ptr_;
    static SemaphoreHandle_t singleton_mutex_;

    // --- Private Methods ---
    esp_err_t add_peer_internal(NodeId node_id,
                                const uint8_t *mac,
                                uint8_t channel,
                                NodeType type);
    esp_err_t remove_peer_internal(NodeId node_id);
    void send_pair_request();
    esp_err_t send_packet(const uint8_t *mac_addr, const void *data, size_t len);

    // Protocol Message Processing
    void handle_pair_request(const RxPacket &packet);
    void handle_pair_response(const RxPacket &packet);
    void handle_heartbeat(const RxPacket &packet);

    // Task functions
    static void rx_dispatch_task(void *arg);
    static void transport_worker_task(void *arg);
    static void pairing_timer_cb(TimerHandle_t xTimer);
    static void periodic_pairing_cb(TimerHandle_t xTimer);

    // Static ESP-NOW callbacks (ISR context)
    static void esp_now_recv_cb(const esp_now_recv_info_t *info,
                                const uint8_t *data,
                                int len);
    static void esp_now_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status);

    // const uint8_t *mac_addr, esp_now_send_status_t status);
};
