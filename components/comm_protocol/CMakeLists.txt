idf_component_register(
    SRCS
        # "protocol_types.cpp"
        "protocol_frame.cpp"
        "protocol_codec.cpp"
    INCLUDE_DIRS
        include
    REQUIRES
        core
)
