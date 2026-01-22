idf_component_register(
    SRCS 
        "test_ultrasonic_sensor.cpp"
    INCLUDE_DIRS 
        "."
    REQUIRES 
        unity
        sensor_ultrasonic
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC -DUNIT_TEST)