idf_component_register(
    
    SRCS 
        "central_hub_app.cpp" "main.cpp"
                    
    INCLUDE_DIRS 
        "include"
        "."
    
    REQUIRES
         wifi_manager
         espnow
         ota_manager
         common
)