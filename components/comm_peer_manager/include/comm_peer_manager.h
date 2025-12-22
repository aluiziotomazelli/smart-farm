#pragma once

#include <stdbool.h>
#include <stdint.h>

void comm_peer_manager_init(void);

bool comm_peer_manager_resolve_peer(uint32_t node_id, uint8_t out_mac[6]);

void comm_peer_manager_on_discovery_announce(uint32_t node_id, const uint8_t mac[6]);

void comm_peer_manager_on_discovery_response(uint32_t node_id, const uint8_t mac[6]);

void comm_peer_manager_on_packet_rx(uint32_t node_id, const uint8_t mac[6]);

void comm_peer_manager_purge(int64_t max_age_us);
