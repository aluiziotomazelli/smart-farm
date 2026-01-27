# components/wifi_manager/CMakeLists.txt
idf_component_register(
    SRCS 
        "wifi_manager.cpp"
                    
    INCLUDE_DIRS 
        "include"
                    
    REQUIRES 
        esp_event 
    
    PRIV_REQUIRES 
        nvs_flash 
        esp_wifi 
        esp_netif 
        freertos
        # common
)
