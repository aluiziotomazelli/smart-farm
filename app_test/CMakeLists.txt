cmake_minimum_required(VERSION 3.16)

# Define the project name
set(PROJECT_NAME "test")

# Add paths for shared components
list(APPEND EXTRA_COMPONENT_DIRS
# ${CMAKE_CURRENT_LIST_DIR}/../managed_components
# "${CMAKE_CURRENT_LIST_DIR}/../components"
"${CMAKE_CURRENT_LIST_DIR}/../components/common"
"${CMAKE_CURRENT_LIST_DIR}/../components/ultrasonic_sensor"
"${CMAKE_CURRENT_LIST_DIR}/../components/floatswitch"
"${CMAKE_CURRENT_LIST_DIR}/../components/nvs_core"
"${CMAKE_CURRENT_LIST_DIR}/../components/power_control"
"${CMAKE_CURRENT_LIST_DIR}/../components/espnow_manager"
"${CMAKE_CURRENT_LIST_DIR}/../components/wifi_manager"
"${CMAKE_CURRENT_LIST_DIR}/../components/gpio_validator"
# "${CMAKE_CURRENT_LIST_DIR}/../components/ota_manager"
)

set(COMPONENTS main)

# Include the ESP-IDF framework
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Define the project
project(${PROJECT_NAME})
