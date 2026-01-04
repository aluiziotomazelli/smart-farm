#include "common_types.hpp"
#include "esp_mac.h"

// DEFINIÇÃO das bases (alocação real)
ESP_EVENT_DEFINE_BASE(APP_WIFI_EVENT);
ESP_EVENT_DEFINE_BASE(APP_ESPNOW_EVENT);
ESP_EVENT_DEFINE_BASE(APP_OTA_EVENT);

namespace common {

uint8_t generate_node_id()
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    return mac[3] ^ mac[4] ^ mac[5];
}

} // namespace common
