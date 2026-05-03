#pragma once

#include <gmock/gmock.h>
#include "interfaces/i_ota_session.hpp"

class MockOtaSession : public IOtaSession {
public:
    MOCK_METHOD(esp_err_t, begin, (const esp_http_client_config_t* config), (override));
    MOCK_METHOD(esp_err_t, get_img_desc, (esp_app_desc_t* new_app_info), (override));
    MOCK_METHOD(esp_err_t, perform, (), (override));
    MOCK_METHOD(bool, is_complete, (), (override));
    MOCK_METHOD(esp_err_t, finish, (), (override));
    MOCK_METHOD(void, abort, (), (override));
};
