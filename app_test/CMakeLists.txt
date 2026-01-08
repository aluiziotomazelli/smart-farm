cmake_minimum_required(VERSION 3.16)

# Define o nome do projeto
set(PROJECT_NAME "test")

# Adiciona caminhos para componentes compartilhados
list(APPEND EXTRA_COMPONENT_DIRS
    # ${CMAKE_CURRENT_LIST_DIR}/main
    ${CMAKE_CURRENT_LIST_DIR}/../components
)

# Inclui o framework ESP-IDF
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Define o projeto
project(${PROJECT_NAME})
