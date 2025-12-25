idf_component_register(SRCS "central_hub_app.cpp" "main.cpp"
                    INCLUDE_DIRS "include")

set(requires "nvs_core")
list(APPEND REQUIRES ${requires})
