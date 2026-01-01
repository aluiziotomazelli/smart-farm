idf_component_register(
    
    SRCS 
        "central_hub_app.cpp" "main.cpp"
                    
    INCLUDE_DIRS 
        "include"
        "."
    
    REQUIRES
         core
         esp_wifi
         wifi_manager
         espnow_comm
         ota_manager
         common_types
)