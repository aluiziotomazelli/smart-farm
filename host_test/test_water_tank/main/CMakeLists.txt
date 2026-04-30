idf_component_register(
    SRCS 
        "main.cpp"
        "test_water_tank.cpp"
        "test_water_tank_geometry.cpp"
        "test_water_tank_logic.cpp"
        "../../../app_water_tank/main/src/tank_geometry.cpp"
        "../../../app_water_tank/main/src/water_tank_logic.cpp"
        "../../../app_water_tank/main/src/water_tank_app.cpp"
        "../../../app_water_tank/main/src/water_tank_nvs.cpp"
    INCLUDE_DIRS 
        "."
        "../../common"
        "../../../app_water_tank/main/include"
        "../../../app_water_tank/main"
    REQUIRES 
        gtest
        common
        ultrasonic_sensor
        floatswitch
        espnow_manager
        power_control
        nvs_core
        WHOLE_ARCHIVE
)

if(IDF_TARGET STREQUAL "linux")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PROJ_HOST_TEST)
    target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
    target_link_options(${COMPONENT_LIB} PUBLIC --coverage)
endif()