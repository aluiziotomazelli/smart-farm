#pragma once

#include <gmock/gmock.h>
#include "interfaces/i_system.hpp"

class MockSystem : public ISystem {
public:
    MOCK_METHOD(void, restart, (), (override));
    MOCK_METHOD(const esp_app_desc_t*, get_running_app_desc, (), (override));
    MOCK_METHOD(esp_err_t, get_partition_sha256, (const esp_partition_t* partition, uint8_t* sha_256), (override));
    MOCK_METHOD(const esp_partition_t*, get_update_partition, (), (override));
};
