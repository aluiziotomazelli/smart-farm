cmake_minimum_required(VERSION 3.16)

# The project currently uses a flat multi-firmware structure.
# For host tests, we want to include the components we are testing.
# List extra component directories so IDF can find our local components.
list(APPEND EXTRA_COMPONENT_DIRS
    "../../components/nvs_core"
    "../../components/common"
    "../common"
    "../gtest"
)

set(COMPONENTS 
    "main"
    "nvs_core"
)


include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(test_nvs_core)

# Set the patterns to extract from the binary
set(EXTRACT_PATTERNS_test_nvs_core "*/components/nvs_core/*")

# Set the patterns to remove from the binary
set(REMOVE_PATTERNS_test_nvs_core "*/components/nvs_core/include/*")

# Include common coverage logic
include("../coverage_common.cmake")
setup_gtest_coverage(test_nvs_core test_nvs_core.elf)