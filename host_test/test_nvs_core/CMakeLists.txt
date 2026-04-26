cmake_minimum_required(VERSION 3.16)

# The project currently uses a flat multi-firmware structure.
# For host tests, we want to include the components we are testing.
set(EXTRA_COMPONENT_DIRS 
    "../../components/nvs_core"
    "../../components/common"
    "../common"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_nvs_core)
