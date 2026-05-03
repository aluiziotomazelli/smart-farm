#pragma once

#include "interfaces/i_manifest_parser.hpp"

class ManifestParser : public IManifestParser {
public:
    std::optional<OtaManifest> parse(const std::string& json_content) const override;
};
