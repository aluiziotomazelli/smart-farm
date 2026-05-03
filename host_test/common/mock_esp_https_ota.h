#pragma once

#include "esp_http_client.h"

// Stub for host-based testing
typedef struct {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
} esp_app_desc_t;

typedef struct {
    // Add fields as needed for tests
    int placeholder;
} esp_https_ota_config_t;

// Mock function signatures or empty definitions as needed
#define ESP_OK 0
