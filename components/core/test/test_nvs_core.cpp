#include "esp_log.h"
#include "nvs_core.hpp"
#include "unity.h"

#include <string.h>

const char *TAG = "NvsCoreTest";

// Mock para expor o que precisamos
class NvsCoreMock : public NvsCore
{
public:
    NvsCoreMock(const char *ns)
        : NvsCore(ns)
    {
    }
    esp_err_t loadAppData() override
    {
        return ESP_OK;
    }
    esp_err_t saveAppData() override
    {
        return ESP_OK;
    }
    void setAppDefaults() override
    {
    }

    // Método para facilitar o teste de gravação direta
    template <typename T> esp_err_t saveStructPublic(const char *key, const T &data)
    {
        return saveStruct(key, data);
    }
};

TEST_CASE("NVS: Warmup System", "[nvs][warmup]")
{
    nvs_flash_init();
    nvs_handle_t h;
    nvs_open("warmup", NVS_READWRITE, &h);
    uint32_t dummy = 0;
    nvs_set_u32(h, "d", dummy); // Força a alocação da tabela de páginas
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGE(TAG,
             "Will FAIL in first run, because first nvs operation leaks memory");
}

TEST_CASE("NVS Core: Lifecycle and Integrity", "[nvs]")
{
    // nvs_warmup_internal();

    NvsCoreMock storage("test_ns");

    // Preparando dados na primeira instância
    CoreStorage &data   = storage.getCoreData();
    data.schema_version = 1;
    data.boot_count     = 10;

    ESP_LOGI(TAG, "Initializing NVS partition");
    storage.init_partition();

    ESP_LOGI(TAG, "Committing data to NVS partition");
    TEST_ASSERT_EQUAL(ESP_OK, storage.commit());

    // Nova instância
    ESP_LOGI(TAG, "Creating new NvsCoreMock instance to verify persistence");
    NvsCoreMock storage2("test_ns");

    // ESSENCIAL: Carregar os dados da Flash para a RAM
    ESP_LOGI(TAG, "Loading data from NVS partition");
    TEST_ASSERT_EQUAL(ESP_OK, storage2.load());

    ESP_LOGI(TAG, "Verifying data integrity");
    TEST_ASSERT_EQUAL(1, storage2.getCoreData().schema_version);
    TEST_ASSERT_EQUAL(10, storage2.getCoreData().boot_count);

    // Limpeza e verificação de Reset
    ESP_LOGI(TAG, "Performing factory reset");
    storage2.factory_reset();
    ESP_LOGI(TAG, "Reloading data after factory reset");
    storage2.load();
    ESP_LOGI(TAG, "Verifying data after factory reset");
    TEST_ASSERT_EQUAL(0, storage2.getCoreData().boot_count);
}
