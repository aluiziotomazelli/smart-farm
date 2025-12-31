#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

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

    // Gerenciamento de credenciais
    esp_err_t storeCredentials(const std::string &ssid, const std::string &password);
    esp_err_t loadCredentials(std::string &ssid, std::string &password);
    bool hasCredentials();
    void clearCredentials();

    // Conexão WiFi
    esp_err_t connectWiFi(uint32_t timeout_ms = 15000);
    esp_err_t connectWiFi(const std::string &ssid,
                          const std::string &password,
                          uint32_t timeout_ms = 15000);
    void disconnectWiFi();

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
    bool wifi_connected_;
    std::string device_type_;

    // Helpers internos
    esp_err_t resolveServerMdns(const std::string &hostname, std::string &url);
    bool waitForIp(uint32_t timeout_ms);

    static const char *TAG;
};

#endif // OTA_MANAGER_H
