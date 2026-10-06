#include "kspch.h"
#include "SourceImportConfigurationYaml.h"
#include <set>

namespace Kans::SourceMetadataDetail
{
    std::variant<std::monostate, AssetError> CheckFields(const YAML::Node&                  node,
                                                         std::initializer_list<const char*> allowed)
    {
        if (!node.IsMap())
            return AssetError {AssetErrorCode::InvalidData, "Metadata block must be a map"};
        std::set<std::string> seen;
        for (const auto& pair : node)
        {
            if (!pair.first.IsScalar())
                return AssetError {AssetErrorCode::InvalidData, "Metadata field name must be scalar"};
            if (pair.first.Scalar().size() > 256)
                return AssetError {AssetErrorCode::InvalidData, "Metadata field name exceeds limit"};
            const auto name = pair.first.as<std::string>();
            if (std::find(allowed.begin(), allowed.end(), name) == allowed.end() || !seen.insert(name).second)
                return AssetError {AssetErrorCode::InvalidData, "Unknown or duplicate metadata field: " + name};
        }
        return std::monostate {};
    }
    std::variant<SourceImportConfiguration, AssetError> DecodeImport(const YAML::Node& node)
    {
        auto checked = CheckFields(node, {"importer", "settings"});
        if (const auto* error = std::get_if<AssetError>(&checked))
            return *error;
        if (!node["importer"].IsScalar() || !node["settings"].IsMap() ||
            node["settings"].size() > SourceImportConfiguration::MaximumSettings)
            return AssetError {AssetErrorCode::InvalidData, "Invalid import configuration fields/count"};
        SourceImportConfiguration result;
        if (node["importer"].Scalar().size() > SourceImportConfiguration::MaximumString)
            return AssetError {AssetErrorCode::InvalidData, "Importer name exceeds limit"};
        result.Importer = node["importer"].as<std::string>();
        size_t bytes    = result.Importer.size();
        for (const auto& pair : node["settings"])
        {
            if (!pair.first.IsScalar() || !pair.second.IsScalar())
                return AssetError {AssetErrorCode::InvalidData, "Import setting key/value must be scalar"};
            const auto& key   = pair.first.Scalar();
            const auto& value = pair.second.Scalar();
            if (key.size() > SourceImportConfiguration::MaximumString ||
                value.size() > SourceImportConfiguration::MaximumString ||
                key.size() > SourceImportConfiguration::MaximumBytes - bytes)
                return AssetError {AssetErrorCode::InvalidData, "Import setting exceeds byte limit"};
            bytes += key.size();
            if (value.size() > SourceImportConfiguration::MaximumBytes - bytes)
                return AssetError {AssetErrorCode::InvalidData, "Import configuration exceeds byte limit"};
            bytes += value.size();
            if (!result.Settings.emplace(pair.first.as<std::string>(), pair.second.as<std::string>()).second)
                return AssetError {AssetErrorCode::InvalidData, "Duplicate import setting"};
        }
        if (!result.IsValid())
            return AssetError {AssetErrorCode::InvalidData, "Import configuration exceeds limits or has invalid names"};
        return result;
    }
    void EncodeImport(YAML::Emitter& emitter, const SourceImportConfiguration& configuration)
    {
        emitter << YAML::BeginMap;
        emitter << YAML::Key << "importer" << YAML::Value << configuration.Importer;
        emitter << YAML::Key << "settings" << YAML::Value << YAML::BeginMap;
        for (const auto& [key, value] : configuration.Settings)
            emitter << YAML::Key << key << YAML::Value << value;
        emitter << YAML::EndMap << YAML::EndMap;
    }
} // namespace Kans::SourceMetadataDetail
