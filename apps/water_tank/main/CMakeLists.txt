
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
        
        
)



# Define o tipo explicitamente
# IMPORTANTE: Adiciona o diretório do app ANTES do core
target_compile_definitions(${COMPONENT_LIB} PRIVATE USE_WATER_TANK_CORE=1)
target_include_directories(${COMPONENT_LIB} BEFORE PRIVATE ".")
