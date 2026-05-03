#include "manifest_parser.hpp"
#include "version_helper.hpp"
#include "cJSON.h"

std::optional<OtaManifest> ManifestParser::parse(const std::string& json_content) const {
    cJSON* root = cJSON_Parse(json_content.c_str());
    if (root == nullptr) {
        return std::nullopt;
    }

    OtaManifest manifest;
    
    cJSON* device_type = cJSON_GetObjectItem(root, "device_type");
    cJSON* version = cJSON_GetObjectItem(root, "version");
    cJSON* firmware_url = cJSON_GetObjectItem(root, "firmware_url");
    cJSON* firmware_size = cJSON_GetObjectItem(root, "firmware_size");
    cJSON* sha256_hex = cJSON_GetObjectItem(root, "sha256_hex");

    bool success = false;
    if (cJSON_IsString(device_type) && 
        cJSON_IsString(version) && 
        cJSON_IsString(firmware_url) && 
        cJSON_IsNumber(firmware_size) && 
        cJSON_IsString(sha256_hex)) {
        
        auto v = VersionHelper::parse(version->valuestring);
        if (v.has_value()) {
            manifest.device_type = device_type->valuestring;
            manifest.version = v.value();
            manifest.firmware_url = firmware_url->valuestring;
            manifest.firmware_size = static_cast<uint32_t>(firmware_size->valuedouble);
            manifest.sha256_hex = sha256_hex->valuestring;
            success = true;
        }
    }

    cJSON_Delete(root);
    return success ? std::make_optional(manifest) : std::nullopt;
}
