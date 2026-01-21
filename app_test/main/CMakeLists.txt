idf_component_register(
    SRCS 
        "test_app.cpp" 
        "main.cpp"
                    
    INCLUDE_DIRS 
        "include"
        
    REQUIRES
        # espnow
        # common
        power_control
        gpio_validator
        
    PRIV_REQUIRES
    
)
