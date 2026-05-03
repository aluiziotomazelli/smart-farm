#pragma once

#include "esp_err.h"
#include <string>

/**
 * @brief Interface for fetching remote resources.
 */
class IHttpClient {
public:
    virtual ~IHttpClient() = default;

    /**
     * @brief Fetches the content from the specified URL.
     * 
     * @param url The URL to fetch.
     * @param output_content String to store the fetched content.
     * @return esp_err_t ESP_OK on success, or an error code.
     */
    virtual esp_err_t fetch(const std::string& url, std::string& output_content) = 0;
};
