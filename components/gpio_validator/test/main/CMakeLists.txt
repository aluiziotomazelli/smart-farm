idf_component_register(
    SRCS
        "main.c" 
        "test_gpio_validator.cpp"
    INCLUDE_DIRS 
        "."
    REQUIRES 
        unity 
        gpio_validator
    WHOLE_ARCHIVE
)

# Move the test source logic here later if needed, 
# for now we keep it in this main component to be the entry point.
