idf_component_register(SRCS "wifi_manager.cpp"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES nvs_flash esp_wifi esp_event esp_netif esp_log freertos)
