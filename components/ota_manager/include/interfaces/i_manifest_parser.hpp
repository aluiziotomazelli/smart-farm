// components/ota_manager/include/interfaces/i_manifest_parser.hpp
#pragma once

#include "../ota_types.hpp"
#include <string>
#include <optional>

class IManifestParser
{
public:
    virtual ~IManifestParser() = default;

    virtual std::optional<OtaManifest> parse(const std::string& json_content) const = 0;
};
