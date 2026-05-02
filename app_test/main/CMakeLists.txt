idf_component_register(
    SRCS 
        "main.cpp"
                    
    INCLUDE_DIRS 
        "include"
        "."
    REQUIRES
        nvs_core
        common
        ultrasonic_sensor
        floatswitch
        espnow_manager
        power_control
        wifi_manager
        gpio_validator
)
