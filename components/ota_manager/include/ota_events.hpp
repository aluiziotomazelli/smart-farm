#pragma once

#include "esp_event.h"

// Declare the event base
ESP_EVENT_DECLARE_BASE(APP_OTA_EVENT);

// Define the OTA events
enum
{
    OTA_CMD_START,
    OTA_CMD_STOP,
    OTA_EVT_STARTED,
    OTA_EVT_FAILED,
    OTA_EVT_FINISHED,
};
