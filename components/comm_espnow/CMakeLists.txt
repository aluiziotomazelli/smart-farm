idf_component_register(
    SRCS
        comm_espnow.cpp
    INCLUDE_DIRS
        include
    REQUIRES
        comm
        esp_wifi
        # esp_now
)
