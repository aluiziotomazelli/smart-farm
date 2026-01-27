idf_component_register(
    SRCS
        "main.cpp"
        "water_tank_app.cpp"
        "water_tank_nvs.cpp"
    INCLUDE_DIRS
        "."    
        "include"
    REQUIRES
        sensor_ultrasonic
        sensor_floatswitch
        core
        power_control
        # espnow_comm
        # ota_manager
        # common_types
)