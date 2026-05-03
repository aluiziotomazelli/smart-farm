#pragma once

#include "esp_err.h"

/**
 * @brief Interface for managing OTA rollback and application state validation.
 */
class IRollbackManager {
public:
    virtual ~IRollbackManager() = default;

    /** @brief Checks if the running application image is in the pending verification state. */
    virtual bool is_pending_verify() = 0;

    /** @brief Marks the current application image as valid, canceling any pending rollback. */
    virtual esp_err_t mark_app_valid() = 0;

    /** @brief Marks the current image as invalid, triggers a rollback, and reboots the device. */
    virtual void rollback_and_reboot() = 0;
};
