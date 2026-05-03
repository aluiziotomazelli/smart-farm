#pragma once

#include "ota_types.hpp"
#include <optional>
#include <cstdlib>
#include <string>

class VersionHelper
{
public:
    static std::optional<OtaVersion> parse(const std::string& version_str)
    {
        OtaVersion version = { 0, 0, 0 };
        const char* cursor = version_str.c_str();
        char* end_ptr = nullptr;

        unsigned long major = std::strtoul(cursor, &end_ptr, 10);
        if (end_ptr == cursor || *end_ptr != '.') {
            return std::nullopt;
        }

        cursor = end_ptr + 1;
        unsigned long minor = std::strtoul(cursor, &end_ptr, 10);
        if (end_ptr == cursor || *end_ptr != '.') {
            return std::nullopt;
        }

        cursor = end_ptr + 1;
        unsigned long patch = std::strtoul(cursor, &end_ptr, 10);
        if (end_ptr == cursor || *end_ptr != '\0') {
            return std::nullopt;
        }

        if (major > UINT16_MAX || minor > UINT16_MAX || patch > UINT16_MAX) {
            return std::nullopt;
        }

        version.major = static_cast<uint16_t>(major);
        version.minor = static_cast<uint16_t>(minor);
        version.patch = static_cast<uint16_t>(patch);
        return version;
    }
};
