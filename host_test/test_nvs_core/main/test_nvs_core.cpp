#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "nvs_core.hpp"
#include "../../common/mock_hal_nvs.hpp"

using ::testing::_;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::SetArgPointee;

// Concrete implementation for testing
class TestNvsCore : public NvsCore
{
public:
    TestNvsCore(IHalNvs& hal)
        : NvsCore("test_ns", hal)
    {
    }

    struct AppData
    {
        int value;
    } app_data;

    esp_err_t loadAppData() override { return loadStruct("app_data", app_data); }

    esp_err_t saveAppData() override { return saveStruct("app_data", app_data); }

    void setAppDefaults() override { app_data.value = 42; }
};

class NvsCoreTest : public ::testing::Test
{
protected:
    MockHalNvs mock_hal;
    TestNvsCore nvs;

    NvsCoreTest()
        : nvs(mock_hal)
    {
    }
};

TEST_F(NvsCoreTest, InitPartitionSuccess)
{
    EXPECT_CALL(mock_hal, hal_nvs_flash_init()).WillOnce(Return(ESP_OK));

    EXPECT_EQ(nvs.init_partition(), ESP_OK);
}

TEST_F(NvsCoreTest, LoadSuccess)
{
    nvs_handle_t fake_handle = 123;

    // Expect open
    EXPECT_CALL(mock_hal, hal_nvs_open(testing::StrEq("test_ns"), NVS_READONLY, _))
        .WillOnce(DoAll(SetArgPointee<2>(fake_handle), Return(ESP_OK)));

    // Expect load core_data
    EXPECT_CALL(mock_hal, hal_nvs_get_blob(fake_handle, testing::StrEq("core_data"), _, _)).WillOnce(Return(ESP_OK));

    // Expect load app_data
    EXPECT_CALL(mock_hal, hal_nvs_get_blob(fake_handle, testing::StrEq("app_data"), _, _)).WillOnce(Return(ESP_OK));

    // Expect close
    EXPECT_CALL(mock_hal, hal_nvs_close(fake_handle));

    EXPECT_EQ(nvs.load(), ESP_OK);
}

TEST_F(NvsCoreTest, CommitSuccess)
{
    nvs_handle_t fake_handle = 123;

    // Expect open
    EXPECT_CALL(mock_hal, hal_nvs_open(testing::StrEq("test_ns"), NVS_READWRITE, _))
        .WillOnce(DoAll(SetArgPointee<2>(fake_handle), Return(ESP_OK)));

    // Expect save core_data
    EXPECT_CALL(mock_hal, hal_nvs_set_blob(fake_handle, testing::StrEq("core_data"), _, _)).WillOnce(Return(ESP_OK));

    // Expect save app_data
    EXPECT_CALL(mock_hal, hal_nvs_set_blob(fake_handle, testing::StrEq("app_data"), _, _)).WillOnce(Return(ESP_OK));

    // Expect commit
    EXPECT_CALL(mock_hal, hal_nvs_commit(fake_handle)).WillOnce(Return(ESP_OK));

    // Expect close
    EXPECT_CALL(mock_hal, hal_nvs_close(fake_handle));

    EXPECT_EQ(nvs.commit(), ESP_OK);
}

TEST_F(NvsCoreTest, InitPartitionErrorCallsEraseAndInit)
{
    testing::InSequence s;

    EXPECT_CALL(mock_hal, hal_nvs_flash_init()).WillOnce(Return(ESP_ERR_NVS_NO_FREE_PAGES));
    EXPECT_CALL(mock_hal, hal_nvs_flash_erase()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_hal, hal_nvs_flash_init()).WillOnce(Return(ESP_OK));

    EXPECT_EQ(nvs.init_partition(), ESP_OK);
}

TEST_F(NvsCoreTest, OpenNvsFailReturnsError)
{
    EXPECT_CALL(mock_hal, hal_nvs_open(testing::StrEq("test_ns"), NVS_READONLY, _))
        .WillOnce(Return(ESP_ERR_NVS_NOT_FOUND));
    EXPECT_EQ(nvs.load(), ESP_ERR_NVS_NOT_FOUND);
}

TEST_F(NvsCoreTest, LoadWithSchemaMismatchMigrates)
{
    nvs_handle_t fake_handle = 123;
    CoreStorage old_data;
    memset(&old_data, 0, sizeof(CoreStorage));
    old_data.schema_version = 0; // Old version

    EXPECT_CALL(mock_hal, hal_nvs_open(_, NVS_READONLY, _))
        .WillOnce(DoAll(SetArgPointee<2>(fake_handle), Return(ESP_OK)));

    // Return data with old version
    EXPECT_CALL(mock_hal, hal_nvs_get_blob(fake_handle, testing::StrEq("core_data"), _, _))
        .WillOnce(Invoke([old_data](nvs_handle_t, const char*, void* out, size_t* len) {
            if (out)
                memcpy(out, &old_data, sizeof(old_data));
            if (len)
                *len = sizeof(old_data);
            return ESP_OK;
        }));

    EXPECT_CALL(mock_hal, hal_nvs_get_blob(fake_handle, testing::StrEq("app_data"), _, _)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_hal, hal_nvs_close(fake_handle));

    EXPECT_EQ(nvs.load(), ESP_OK);
    EXPECT_EQ(nvs.getCoreData().schema_version, CORE_SCHEMA_VERSION);
}

TEST_F(NvsCoreTest, LoadWithAppDataFailReturnsError)
{
    nvs_handle_t fake_handle = 123;
    EXPECT_CALL(mock_hal, hal_nvs_open(_, NVS_READONLY, _))
        .WillOnce(DoAll(SetArgPointee<2>(fake_handle), Return(ESP_OK)));

    EXPECT_CALL(mock_hal, hal_nvs_get_blob(fake_handle, testing::StrEq("core_data"), _, _)).WillOnce(Return(ESP_OK));

    // Simulate app data load failure
    EXPECT_CALL(mock_hal, hal_nvs_get_blob(fake_handle, testing::StrEq("app_data"), _, _))
        .WillOnce(Return(ESP_ERR_NVS_NOT_FOUND));

    EXPECT_CALL(mock_hal, hal_nvs_close(fake_handle));

    EXPECT_EQ(nvs.load(), ESP_ERR_NVS_NOT_FOUND);
}

TEST_F(NvsCoreTest, EraseNamespaceSuccess)
{
    nvs_handle_t fake_handle = 123;
    testing::InSequence s;

    EXPECT_CALL(mock_hal, hal_nvs_open(testing::StrEq("test_ns"), NVS_READWRITE, _))
        .WillOnce(DoAll(SetArgPointee<2>(fake_handle), Return(ESP_OK)));

    EXPECT_CALL(mock_hal, hal_nvs_erase_all(fake_handle)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_hal, hal_nvs_commit(fake_handle)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_hal, hal_nvs_close(fake_handle));

    EXPECT_EQ(nvs.erase_namespace(), ESP_OK);
}

TEST_F(NvsCoreTest, FactoryResetSequence)
{
    nvs_handle_t fake_handle = 123;
    testing::InSequence s;

    // 1. Erase Namespace
    EXPECT_CALL(mock_hal, hal_nvs_open(testing::StrEq("test_ns"), NVS_READWRITE, _))
        .WillOnce(DoAll(SetArgPointee<2>(fake_handle), Return(ESP_OK)));
    EXPECT_CALL(mock_hal, hal_nvs_erase_all(fake_handle)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_hal, hal_nvs_commit(fake_handle)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_hal, hal_nvs_close(fake_handle));

    // 2. Commit Defaults (factory_reset calls apply_core_defaults, setAppDefaults then commit)
    EXPECT_CALL(mock_hal, hal_nvs_open(testing::StrEq("test_ns"), NVS_READWRITE, _))
        .WillOnce(DoAll(SetArgPointee<2>(fake_handle), Return(ESP_OK)));
    EXPECT_CALL(mock_hal, hal_nvs_set_blob(fake_handle, testing::StrEq("core_data"), _, _)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_hal, hal_nvs_set_blob(fake_handle, testing::StrEq("app_data"), _, _)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_hal, hal_nvs_commit(fake_handle)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_hal, hal_nvs_close(fake_handle));

    nvs.factory_reset();

    // Verify that data is reset in memory
    EXPECT_EQ(nvs.getCoreData().schema_version, CORE_SCHEMA_VERSION);
    EXPECT_EQ(nvs.app_data.value, 42); // Value from setAppDefaults
}
