cmake_minimum_required(VERSION 3.16)

# Define the project name
set(PROJECT_NAME "test")

# Add paths for shared components
list(APPEND EXTRA_COMPONENT_DIRS
    # ${CMAKE_CURRENT_LIST_DIR}/main
    ${CMAKE_CURRENT_LIST_DIR}/../components/common
    ${CMAKE_CURRENT_LIST_DIR}/../components/espnow
    # ${CMAKE_CURRENT_LIST_DIR}/../components/power_control
    # ${CMAKE_CURRENT_LIST_DIR}/../components/gpio_validator
    # ${CMAKE_CURRENT_LIST_DIR}/../components/sensor_floatswitch
)

# Include the ESP-IDF framework
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Define the project
project(${PROJECT_NAME})
