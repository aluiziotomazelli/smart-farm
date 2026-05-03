#include "rollback_manager.hpp"
#include "esp_ota_ops.h"

bool RollbackManager::is_pending_verify() {
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK) {
        return state == ESP_OTA_IMG_PENDING_VERIFY;
    }
    return false;
}

esp_err_t RollbackManager::mark_app_valid() {
    return esp_ota_mark_app_valid_cancel_rollback();
}

void RollbackManager::rollback_and_reboot() {
    esp_ota_mark_app_invalid_rollback_and_reboot();
}
