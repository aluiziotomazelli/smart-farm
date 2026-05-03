#pragma once

#include "ota_types.hpp"

/**
 * @brief Public interface for the OTA Manager component.
 *
 * Provides a passive, dependency-injected API for managing the OTA update flow.
 */
class IOtaManager {
public:
    virtual ~IOtaManager() = default;

    /** @brief Initializes the OTA manager with the provided configuration. */
    virtual bool init(const OtaConfig& config) = 0;

    /** @brief Cleans up resources used by the OTA manager. */
    virtual void deinit() = 0;

    /** @brief Initiates the OTA process in the background. */
    virtual bool start_ota() = 0;

    /** @brief Cancels an ongoing OTA process. */
    virtual void cancel_ota() = 0;

    /** @brief Returns the current status of the OTA manager. */
    virtual OtaStatus get_status() const = 0;

    /** @brief Checks if a newly downloaded image is pending verification. */
    virtual bool check_pending_verify() const = 0;

    /** @brief Confirms that the current application image is valid and cancels any pending rollback. */
    virtual bool confirm_app_valid() = 0;

    /** @brief Marks the current image as invalid and requests a rollback and reboot. */
    virtual void rollback_and_reboot() = 0;
};

