#include "common_types.hpp"
#include "esp_mac.h"

EventGroupHandle_t sys_events = nullptr;

namespace common {

uint8_t generate_node_id()
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    return mac[3] ^ mac[4] ^ mac[5];
}

} // namespace common

xEventGroup_t event_group;
