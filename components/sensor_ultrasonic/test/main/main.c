#include "esp_heap_caps.h"
#include "unity.h"
#include "unity_test_runner.h"
#include <stdio.h>

#define TEST_MEMORY_LEAK_THRESHOLD (-500)

static size_t before_free_8bit;
static size_t before_free_32bit;

static void check_leak(size_t before_free, size_t after_free, const char *type)
{
    ssize_t delta = (ssize_t)after_free - (ssize_t)before_free;
    printf("MALLOC_CAP_%s: Before %u bytes free, After %u bytes free (delta %d)\n",
           type, (unsigned int)before_free, (unsigned int)after_free, (int)delta);
    TEST_ASSERT_MESSAGE(delta >= TEST_MEMORY_LEAK_THRESHOLD, "memory leak");
}

void setUp(void)
{
    before_free_8bit  = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    before_free_32bit = heap_caps_get_free_size(MALLOC_CAP_32BIT);
}

void tearDown(void)
{
    size_t after_free_8bit  = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t after_free_32bit = heap_caps_get_free_size(MALLOC_CAP_32BIT);
    check_leak(before_free_8bit, after_free_8bit, "8BIT");
    check_leak(before_free_32bit, after_free_32bit, "32BIT");
}

#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    // Disable Task Watchdog to avoid triggers in QEMU/Unity menu loop
    esp_task_wdt_deinit();

    // Give some time for QEMU UART to stabilize
    vTaskDelay(pdMS_TO_TICKS(100));

    unity_run_menu();
}
