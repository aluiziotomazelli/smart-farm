idf_component_register(
    SRCS
        "main.c" 
        "test_sensor_ultrasonic.cpp"
    INCLUDE_DIRS 
        "."
    REQUIRES 
        unity 
        gpio_validator
        sensor_ultrasonic
    WHOLE_ARCHIVE
)

# Move the test source logic here later if needed, 
# for now we keep it in this main component to be the entry point.
