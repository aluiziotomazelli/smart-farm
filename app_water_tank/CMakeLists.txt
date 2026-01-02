cmake_minimum_required(VERSION 3.16)

# Define o nome do projeto
set(PROJECT_NAME "water_tank")

# Adiciona caminhos para componentes compartilhados
list(APPEND EXTRA_COMPONENT_DIRS
# ${CMAKE_CURRENT_LIST_DIR}/../managed_components
"${CMAKE_CURRENT_LIST_DIR}/../components"
# "${CMAKE_CURRENT_LIST_DIR}/../components/sensor_ultrasonic"
# "${CMAKE_CURRENT_LIST_DIR}/../components/sensor_floatswitch"
# "${CMAKE_CURRENT_LIST_DIR}/../components/core"
# "${CMAKE_CURRENT_LIST_DIR}/../components/power_control"
# "${CMAKE_CURRENT_LIST_DIR}/../components/espnow_comm"
# "${CMAKE_CURRENT_LIST_DIR}/../components/common_types"
# "${CMAKE_CURRENT_LIST_DIR}/../components/ota_manager"
)

# Inclui o framework ESP-IDF
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Define o projeto
project(${PROJECT_NAME})