idf_component_register(
    SRCS 
        "test_nvs_core.cpp"
    INCLUDE_DIRS 
        "."
    REQUIRES 
        unity
        core
)

# target_compile_definitions(${COMPONENT_LIB} PUBLIC -DUNIT_TEST)