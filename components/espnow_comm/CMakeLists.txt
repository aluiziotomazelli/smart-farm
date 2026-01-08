idf_component_register(
    SRCS
    
    INCLUDE_DIRS
        "include"
    
    REQUIRES
        esp_wifi
        esp_timer
        nvs_flash
        driver
        common_types
    
    PRIV_REQUIRES
        esp_common
)
