idf_component_register(
    SRCS
        "espnow.cpp"
        "espnow_storage.cpp"
    
    INCLUDE_DIRS
        "include"
    
    REQUIRES
        esp_wifi
        esp_timer
        nvs_flash
        driver
        common
    
    PRIV_REQUIRES
        esp_common
)