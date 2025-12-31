idf_component_register(SRCS "ota_sender_app.cpp" "main.cpp"
                    INCLUDE_DIRS "include")

set(requires "espnow_comm" "wifi_manager")

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    # Don't add these to the build unless this app is selected.
    list(APPEND REQUIRES ${requires})
endif()
