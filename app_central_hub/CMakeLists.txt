cmake_minimum_required(VERSION 3.16)

# Define o nome do projeto
set(PROJECT_NAME "central_hub")

set(SDKCONFIG_DEFAULTS
    ${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.esp32  # Chip primeiro
    ${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults          # App depois
)

# Adiciona caminhos para componentes compartilhados
list(APPEND EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/main
    ${CMAKE_CURRENT_LIST_DIR}/../components
    ${CMAKE_CURRENT_LIST_DIR}/../managed_components
)

# Configurações específicas do central_hub
if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults)
    set(SDKCONFIG_DEFAULTS ${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults)
endif()

# Inclui o framework ESP-IDF
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Define o projeto
project(${PROJECT_NAME})