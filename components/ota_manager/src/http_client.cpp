#include "http_client.hpp"
#include "esp_http_client.h"
#include "esp_log.h"

static const char* TAG = "HttpClient";

esp_err_t HttpClient::fetch(const std::string& url, std::string& output_content)
{
    output_content.clear();

    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.timeout_ms = 5000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP client: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0) {
        ESP_LOGW(TAG, "Content-Length not provided, reading until EOF");
    }

    const int status_code = esp_http_client_get_status_code(client);
    if (status_code < 200 || status_code >= 300) {
        ESP_LOGE(TAG, "Unexpected HTTP status code: %d", status_code);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    if (content_length > 0) {
        output_content.reserve(static_cast<size_t>(content_length));
    }

    char buffer[512];
    int total_read = 0;

    while (true) {
        const int read_len = esp_http_client_read(client, buffer, sizeof(buffer));
        if (read_len < 0) {
            ESP_LOGE(TAG, "Failed to read HTTP body");
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }

        if (read_len == 0) {
            break;
        }

        output_content.append(buffer, read_len);
        total_read += read_len;

        if (content_length > 0 && total_read >= content_length) {
            break;
        }
    }

    esp_http_client_cleanup(client);

    if (content_length > 0 && total_read != content_length) {
        ESP_LOGE(TAG, "Incomplete HTTP body: read %d of %d bytes", total_read, content_length);
        output_content.clear();
        return ESP_FAIL;
    }

    return ESP_OK;
}
