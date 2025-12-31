#pragma once

#include "esp_err.h"
#include <cstdint>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class OtaManager
{
public:
    // Singleton
    static OtaManager *getInstance();

    // Prevent copying
    OtaManager(const OtaManager &)            = delete;
    OtaManager &operator=(const OtaManager &) = delete;

    // OTA
    esp_err_t performOta(const std::string &url);
    esp_err_t performOtaWithMdns(const std::string &hostname,
                                 uint16_t port           = 8070,
                                 const std::string &path = "/firmware.bin");

    // Configuração
    void setDeviceType(const std::string &device_type);
    std::string getDeviceType() const;

private:
    OtaManager();
    ~OtaManager();

    // Singleton
    static OtaManager *instance_;
    static SemaphoreHandle_t instance_mutex_;

    // Estado
    std::string device_type_;

    // Helpers internos
    esp_err_t resolveServerMdns(const std::string &hostname, std::string &url);

    static const char *TAG;
};
