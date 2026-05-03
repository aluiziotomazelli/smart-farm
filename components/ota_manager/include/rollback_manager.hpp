#pragma once

#include "interfaces/i_rollback_manager.hpp"

/**
 * @brief Concrete implementation of IRollbackManager.
 */
class RollbackManager : public IRollbackManager {
public:
    /** @copydoc IRollbackManager::is_pending_verify */
    bool is_pending_verify() override;
    /** @copydoc IRollbackManager::mark_app_valid */
    esp_err_t mark_app_valid() override;
    /** @copydoc IRollbackManager::rollback_and_reboot */
    void rollback_and_reboot() override;
};
