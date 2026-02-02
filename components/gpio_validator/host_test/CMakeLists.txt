cmake_minimum_required(VERSION 3.16)

# Force the components we want to include in the host test
set(COMPONENTS main gpio_validator)

# Add the directory containing the component under test
list(APPEND EXTRA_COMPONENT_DIRS "..")

# Add the mocks directory if needed (e.g. for drivers)
# list(APPEND EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/tools/mocks/driver")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_gpio_validator_host)
