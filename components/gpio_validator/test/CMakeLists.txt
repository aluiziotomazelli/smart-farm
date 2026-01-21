cmake_minimum_required(VERSION 3.16)

# Define o nome do projeto de teste
set(PROJECT_NAME "gpio_validator_test")

# Adiciona o componente power_control ao path
list(APPEND EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/..
)

# Inclui o framework ESP-IDF
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Define o projeto
project(${PROJECT_NAME})
