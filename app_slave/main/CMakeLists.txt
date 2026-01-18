idf_component_register(
    SRCS 
        "app_slave.cpp" "main.cpp"
                    
    INCLUDE_DIRS 
        "include"
                    
    REQUIRES 
        wifi_manager 
        espnow 
        common
)
