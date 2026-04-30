cmake_minimum_required(VERSION 3.16)

# The project currently uses a flat multi-firmware structure.
# For host tests, we want to include the components we are testing.
# List extra component directories so IDF can find our local components.
list(APPEND EXTRA_COMPONENT_DIRS

    "../../app_water_tank/managed_components"
    "../../components/common"
    "../../components/nvs_core"
    "../common"
    "../gtest"
    
    "$ENV{IDF_PATH}/tools/mocks/driver"  # Official ESP-IDF driver mocks
    "$ENV{IDF_PATH}/tools/mocks/esp_wifi"  # Official ESP-IDF driver mocks
    "$ENV{IDF_PATH}/tools/mocks/esp_netif"
    "$ENV{IDF_PATH}/tools/mocks/esp_event"
    "$ENV{IDF_PATH}/tools/mocks/lwip"
    # "$ENV{IDF_PATH}/tools/mocks/esp_timer" # esp_timer mocks
)

set(COMPONENTS 
    "main"
    "common"
    "nvs_core"
    "ultrasonic_sensor"
    "floatswitch"
    "espnow_manager"
    "power_control"
)


include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(test_water_tank)

# Set the patterns to extract from the binary
set(EXTRACT_PATTERNS_test_water_tank "*/app_water_tank/*")

# Set the patterns to remove from the binary
set(REMOVE_PATTERNS_test_water_tank "*/app_water_tank/include/*")

# Include common coverage logic
include("../coverage_common.cmake")
setup_gtest_coverage(test_water_tank test_water_tank.elf)