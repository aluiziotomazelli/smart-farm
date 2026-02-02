idf_component_register(SRCS "test_gpio_validator_host.cpp"
                    REQUIRES gpio_validator unity)

# In a real environment with CMock and Ruby:
# idf_component_mock(INCLUDE_DIRS "$ENV{IDF_PATH}/components/esp_hw_support/include"
#                    MOCK_HEADER_FILES "$ENV{IDF_PATH}/components/esp_hw_support/include/esp_chip_info.h")
