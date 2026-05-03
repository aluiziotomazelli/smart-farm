#pragma once

#include <cstdint>
#include <string>

enum class OtaStatus {
    IDLE,
    MANIFEST_FETCH,
    VERSION_CHECK,
    DOWNLOADING,
    VERIFYING,
    READY_TO_RESTART,
    FAILED,
    PENDING_VERIFY
};

struct OtaVersion {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
};

struct OtaManifest {
    std::string device_type;
    OtaVersion version;
    std::string firmware_url;
    uint32_t firmware_size;
    std::string sha256_hex;
};

struct OtaConfig {
    std::string device_type;
    std::string manifest_url;
    uint32_t task_stack_size;
    uint8_t task_priority;
    uint32_t http_timeout_ms;
    bool allow_same_version;
    bool restart_on_success;
};
