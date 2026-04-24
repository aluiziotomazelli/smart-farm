cmake_minimum_required(VERSION 3.16)

# Define the project name
set(PROJECT_NAME "central_hub")

# Add paths for shared components
list(APPEND EXTRA_COMPONENT_DIRS
    # ${CMAKE_CURRENT_LIST_DIR}/main
    ${CMAKE_CURRENT_LIST_DIR}/../components
    # ${CMAKE_CURRENT_LIST_DIR}/../managed_components
)

# Include the ESP-IDF framework
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Define the project
project(${PROJECT_NAME})