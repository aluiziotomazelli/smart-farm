#pragma once

#include <gmock/gmock.h>
#include "interfaces/i_rollback_manager.hpp"

class MockRollbackManager : public IRollbackManager {
public:
    MOCK_METHOD(bool, is_pending_verify, (), (override));
    MOCK_METHOD(esp_err_t, mark_app_valid, (), (override));
    MOCK_METHOD(void, rollback_and_reboot, (), (override));
};
