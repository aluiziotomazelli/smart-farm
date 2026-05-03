#pragma once

#include <gmock/gmock.h>
#include "interfaces/i_http_client.hpp"

class MockHttpClient : public IHttpClient {
public:
    MOCK_METHOD(esp_err_t, fetch, (const std::string& url, std::string& output_content), (override));
};
