#pragma once

#include "interfaces/i_task_scheduler.hpp"

/**
 * @brief Concrete implementation of ITaskScheduler.
 */
class TaskScheduler : public ITaskScheduler
{
public:
    /** @copydoc ITaskScheduler::task_delay() */
    void task_delay(TickType_t xTicksToWait) override { vTaskDelay(xTicksToWait); }

    /** @copydoc ITaskScheduler::create_task() */
    BaseType_t create_task(
        TaskFunction_t pxTaskCode,
        const char* pcName,
        uint32_t usStackDepth,
        void* pvParameters,
        UBaseType_t uxPriority,
        TaskHandle_t* pxCreatedTask) override
    {
        return xTaskCreate(pxTaskCode, pcName, usStackDepth, pvParameters, uxPriority, pxCreatedTask);
    }

    /** @copydoc ITaskScheduler::delete_task() */
    void delete_task(TaskHandle_t xTask) override { vTaskDelete(xTask); }

    /** @copydoc ITaskScheduler::notify_task() */
    BaseType_t notify_task(TaskHandle_t xTask, uint32_t ulValue, eNotifyAction eAction) override
    {
        return xTaskNotify(xTask, ulValue, eAction);
    }

    /** @copydoc ITaskScheduler::task_notify_wait() */
    BaseType_t task_notify_wait(
        uint32_t ulBitsToClearOnEntry,
        uint32_t ulBitsToClearOnExit,
        uint32_t* pulNotificationValue,
        TickType_t xTicksToWait) override
    {
        return xTaskNotifyWait(ulBitsToClearOnEntry, ulBitsToClearOnExit, pulNotificationValue, xTicksToWait);
    }

    /** @copydoc ITaskScheduler::mutex_create() */
    SemaphoreHandle_t mutex_create() override { return xSemaphoreCreateMutex(); }

    /** @copydoc ITaskScheduler::semaphore_binary_create() */
    SemaphoreHandle_t semaphore_binary_create() override { return xSemaphoreCreateBinary(); }

    /** @copydoc ITaskScheduler::semaphore_take() */
    BaseType_t semaphore_take(SemaphoreHandle_t semaphore_handle, TickType_t xTicksToWait) override
    {
        return xSemaphoreTake(semaphore_handle, xTicksToWait);
    }

    /** @copydoc ITaskScheduler::semaphore_give() */
    BaseType_t semaphore_give(SemaphoreHandle_t semaphore_handle) override { return xSemaphoreGive(semaphore_handle); }

    /** @copydoc ITaskScheduler::semaphore_delete() */
    void semaphore_delete(SemaphoreHandle_t semaphore_handle) override { vSemaphoreDelete(semaphore_handle); }
};
