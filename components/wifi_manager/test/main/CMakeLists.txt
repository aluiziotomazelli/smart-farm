# components/wifi_manager/test/main/CMakeLists.txt
idf_component_register(
    SRCS
        "main.c"
        "test_wifi_manager.cpp"
    INCLUDE_DIRS 
        "."
    REQUIRES 
        unity 
        wifi_manager
        nvs_flash
    WHOLE_ARCHIVE
)

# Move the test source logic here later if needed, 
# for now we keep it in this main component to be the entry point.
