#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

class ITaskScheduler
{
public:
    virtual ~ITaskScheduler() = default;

    /** @copydoc vTaskDelay() */
    virtual void task_delay(TickType_t xTicksToWait) = 0;

    /** @copydoc xTaskCreate() */
    virtual BaseType_t create_task(
        TaskFunction_t pxTaskCode,
        const char* pcName,
        uint32_t usStackDepth,
        void* pvParameters,
        UBaseType_t uxPriority,
        TaskHandle_t* pxCreatedTask) = 0;

    /** @copydoc vTaskDelete() */
    virtual void delete_task(TaskHandle_t xTask) = 0;

    /** @copydoc xTaskNotify() */
    virtual BaseType_t notify_task(TaskHandle_t xTask, uint32_t ulValue, eNotifyAction eAction) = 0;

    /** @copydoc xTaskNotifyWait() */
    virtual BaseType_t task_notify_wait(
        uint32_t ulBitsToClearOnEntry,
        uint32_t ulBitsToClearOnExit,
        uint32_t* pulNotificationValue,
        TickType_t xTicksToWait) = 0;

    /** @copydoc xSemaphoreCreateMutex() */
    virtual SemaphoreHandle_t mutex_create() = 0;

    /** @copydoc xSemaphoreCreateBinary() */
    virtual SemaphoreHandle_t semaphore_binary_create() = 0;

    /** @copydoc xSemaphoreTake() */
    virtual BaseType_t semaphore_take(SemaphoreHandle_t semaphore_handle, TickType_t xTicksToWait) = 0;

    /** @copydoc xSemaphoreGive() */
    virtual BaseType_t semaphore_give(SemaphoreHandle_t semaphore_handle) = 0;

    /** @copydoc vSemaphoreDelete() */
    virtual void semaphore_delete(SemaphoreHandle_t semaphore_handle) = 0;
};
