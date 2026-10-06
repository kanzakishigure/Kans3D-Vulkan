#pragma once
#include <initializer_list>
#include <variant>
#include <yaml-cpp/yaml.h>
#include "AssetError.h"
#include "SourceImportConfiguration.h"

namespace Kans::SourceMetadataDetail
{
    // Shared tool serialization helpers; not part of runtime Product loading.
    [[nodiscard]] std::variant<std::monostate, AssetError>            CheckFields(const YAML::Node&                  node,
                                                                                  std::initializer_list<const char*> allowed);
    [[nodiscard]] std::variant<SourceImportConfiguration, AssetError> DecodeImport(const YAML::Node& node);
    void EncodeImport(YAML::Emitter& emitter, const SourceImportConfiguration& configuration);
} // namespace Kans::SourceMetadataDetail
