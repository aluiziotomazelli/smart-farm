idf_component_register(
    SRCS
        "main.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        ota_manager
        common_types
        nvs_flash
        wifi_manager
        driver

)