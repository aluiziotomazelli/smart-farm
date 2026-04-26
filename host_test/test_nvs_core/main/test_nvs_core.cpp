#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "nvs_core.hpp"
#include "mock_hal_nvs.hpp"

using ::testing::_;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::DoAll;

// Concrete implementation for testing
class TestNvsCore : public NvsCore {
public:
    TestNvsCore(IHalNvs& hal) : NvsCore("test_ns", hal) {}
    
    struct AppData {
        int value;
    } app_data;

    esp_err_t loadAppData() override {
        return loadStruct("app_data", app_data);
    }

    esp_err_t saveAppData() override {
        return saveStruct("app_data", app_data);
    }

    void setAppDefaults() override {
        app_data.value = 42;
    }
};

class NvsCoreTest : public ::testing::Test {
protected:
    MockHalNvs mock_hal;
    TestNvsCore nvs;

    NvsCoreTest() : nvs(mock_hal) {}
};

TEST_F(NvsCoreTest, InitPartitionSuccess) {
    EXPECT_CALL(mock_hal, hal_nvs_flash_init())
        .WillOnce(Return(ESP_OK));
    
    EXPECT_EQ(nvs.init_partition(), ESP_OK);
}

TEST_F(NvsCoreTest, LoadSuccess) {
    nvs_handle_t fake_handle = 123;
    
    // Expect open
    EXPECT_CALL(mock_hal, hal_nvs_open(testing::StrEq("test_ns"), NVS_READONLY, _))
        .WillOnce(DoAll(SetArgPointee<2>(fake_handle), Return(ESP_OK)));
    
    // Expect load core_data
    EXPECT_CALL(mock_hal, hal_nvs_get_blob(fake_handle, testing::StrEq("core_data"), _, _))
        .WillOnce(Return(ESP_OK));
        
    // Expect load app_data
    EXPECT_CALL(mock_hal, hal_nvs_get_blob(fake_handle, testing::StrEq("app_data"), _, _))
        .WillOnce(Return(ESP_OK));
    
    // Expect close
    EXPECT_CALL(mock_hal, hal_nvs_close(fake_handle));
    
    EXPECT_EQ(nvs.load(), ESP_OK);
}

TEST_F(NvsCoreTest, CommitSuccess) {
    nvs_handle_t fake_handle = 123;
    
    // Expect open
    EXPECT_CALL(mock_hal, hal_nvs_open(testing::StrEq("test_ns"), NVS_READWRITE, _))
        .WillOnce(DoAll(SetArgPointee<2>(fake_handle), Return(ESP_OK)));
    
    // Expect save core_data
    EXPECT_CALL(mock_hal, hal_nvs_set_blob(fake_handle, testing::StrEq("core_data"), _, _))
        .WillOnce(Return(ESP_OK));
        
    // Expect save app_data
    EXPECT_CALL(mock_hal, hal_nvs_set_blob(fake_handle, testing::StrEq("app_data"), _, _))
        .WillOnce(Return(ESP_OK));
    
    // Expect commit
    EXPECT_CALL(mock_hal, hal_nvs_commit(fake_handle))
        .WillOnce(Return(ESP_OK));
        
    // Expect close
    EXPECT_CALL(mock_hal, hal_nvs_close(fake_handle));
    
    EXPECT_EQ(nvs.commit(), ESP_OK);
}
