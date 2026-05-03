#pragma once

#include "interfaces/i_http_client.hpp"

/**
 * @brief Concrete implementation of IHttpClient.
 */
class HttpClient : public IHttpClient {
public:
    /** @copydoc IHttpClient::fetch */
    esp_err_t fetch(const std::string& url, std::string& output_content) override;
};
