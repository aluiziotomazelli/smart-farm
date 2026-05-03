#pragma once

#include <gmock/gmock.h>
#include "interfaces/i_task_scheduler.hpp"

class MockTaskScheduler : public ITaskScheduler {
public:
    MOCK_METHOD(void, task_delay, (TickType_t xTicksToWait), (override));
    MOCK_METHOD(BaseType_t, create_task, (TaskFunction_t pxTaskCode, const char* pcName, 
                                          uint32_t usStackDepth, void* pvParameters, 
                                          UBaseType_t uxPriority, TaskHandle_t* pxCreatedTask), (override));
    MOCK_METHOD(void, delete_task, (TaskHandle_t xTask), (override));
    MOCK_METHOD(BaseType_t, notify_task, (TaskHandle_t xTask, uint32_t ulValue, eNotifyAction eAction), (override));
    MOCK_METHOD(BaseType_t, task_notify_wait, (uint32_t ulBitsToClearOnEntry, uint32_t ulBitsToClearOnExit, 
                                              uint32_t* pulNotificationValue, TickType_t xTicksToWait), (override));
    MOCK_METHOD(SemaphoreHandle_t, mutex_create, (), (override));
    MOCK_METHOD(SemaphoreHandle_t, semaphore_binary_create, (), (override));
    MOCK_METHOD(BaseType_t, semaphore_take, (SemaphoreHandle_t semaphore_handle, TickType_t xTicksToWait), (override));
    MOCK_METHOD(BaseType_t, semaphore_give, (SemaphoreHandle_t semaphore_handle), (override));
    MOCK_METHOD(void, semaphore_delete, (SemaphoreHandle_t semaphore_handle), (override));
};
