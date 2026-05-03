#pragma once

#include "interfaces/i_ota_manager.hpp"
#include "interfaces/i_manifest_parser.hpp"
#include "interfaces/i_http_client.hpp"
#include "interfaces/i_ota_session.hpp"
#include "interfaces/i_system.hpp"
#include "interfaces/i_task_scheduler.hpp"
#include "interfaces/i_rollback_manager.hpp"

struct OtaDependencies
{
    IHttpClient& http_client;
    IManifestParser& manifest_parser;
    IOtaSession& ota_session;
    ISystem& system;
    ITaskScheduler& task_scheduler;
    IRollbackManager& rollback_manager;
};

class OtaManager : public IOtaManager
{
public:
    OtaManager(const OtaDependencies& deps, const OtaConfig& config);
    ~OtaManager() override;

    bool init(const OtaConfig& config) override;
    void deinit() override;
    bool start_ota() override;
    void cancel_ota() override;
    OtaStatus get_status() const override;
    bool check_pending_verify() const override;
    bool confirm_app_valid() override;
    void rollback_and_reboot() override;

private:
    OtaDependencies deps_;
    OtaConfig config_;
    OtaStatus status_;
    TaskHandle_t ota_task_handle_ = nullptr;

    bool cancel_requested_ = false;
    SemaphoreHandle_t state_mutex_ = nullptr;
    SemaphoreHandle_t shutdown_done_ = nullptr;

    static void ota_task_func(void* pvParameters);
    void ota_task();

    bool should_cancel() const;
    void set_status(OtaStatus status);
    void set_cancel_requested(bool value);
    void signal_shutdown_done();
};
