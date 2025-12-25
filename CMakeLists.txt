cmake_minimum_required(VERSION 3.16)

# Available applications: water_tank, central_hub
set(APP water_tank CACHE STRING "Application to build")

set(EXTRA_COMPONENT_DIRS
    ${CMAKE_SOURCE_DIR}/apps/${APP}
    ${CMAKE_SOURCE_DIR}/components
    
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(smart-farm)
