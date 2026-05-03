#pragma once

#include <gmock/gmock.h>
#include "interfaces/i_manifest_parser.hpp"

class MockManifestParser : public IManifestParser {
public:
    MOCK_METHOD(std::optional<OtaManifest>, parse, (const std::string& json_content), (const, override));
};
