#include "kspch.h"
#include "LocalIDMap.h"

#include <limits>
#include <set>
#include <yaml-cpp/yaml.h>

namespace Kans
{
    namespace
    {
        constexpr size_t kMaximumKeyBytes      = 4096;
        constexpr size_t kMaximumEntries       = 1000000;
        constexpr size_t kMaximumManifestBytes = 64 * 1024 * 1024;

        bool ValidKey(const std::string& key)
        {
            return !key.empty() && key.size() <= kMaximumKeyBytes &&
                   key.find_first_of(std::string("\0\r\n", 3)) == std::string::npos;
        }

        std::variant<uint64_t, AssetError> ValidateMap(const LocalIDMap& ids)
        {
            if (ids.size() > kMaximumEntries)
                return AssetError {AssetErrorCode::InvalidData, "Too many LocalID entries"};
            std::set<uint64_t> used;
            uint64_t           maximum = 0;
            for (const auto& entry : ids)
            {
                if (!ValidKey(entry.first) || !entry.second)
                    return AssetError {AssetErrorCode::InvalidData, "Invalid LocalID mapping entry"};
                if (!used.insert(entry.second).second)
                    return AssetError {AssetErrorCode::DuplicateID, "LocalID assigned to multiple object keys"};
                maximum = std::max(maximum, entry.second);
            }
            return maximum;
        }
    } // namespace

    std::variant<LocalIDMap, AssetError> AllocateLocalIDs(const LocalIDMap&               previous,
                                                          const std::vector<std::string>& objectKeys)
    {
        auto validated = ValidateMap(previous);
        if (const auto* error = std::get_if<AssetError>(&validated))
            return *error;
        if (objectKeys.size() > kMaximumEntries)
            return AssetError {AssetErrorCode::InvalidArgument, "Too many source object keys"};
        std::set<std::string> keys;
        for (const auto& key : objectKeys)
        {
            if (!ValidKey(key))
                return AssetError {AssetErrorCode::InvalidArgument, "Invalid source object key"};
            if (!keys.insert(key).second)
                return AssetError {AssetErrorCode::DuplicateID, "Ambiguous source object key: " + key};
        }
        auto candidate = previous;
        auto maximum   = std::get<uint64_t>(validated);
        // Sorted allocation makes the first import independent of traversal order.
        for (const auto& key : keys)
        {
            if (candidate.count(key))
                continue;
            if (maximum == std::numeric_limits<uint64_t>::max() || candidate.size() == kMaximumEntries)
                return AssetError {AssetErrorCode::InvalidData, "LocalID allocation exhausted"};
            candidate.emplace(key, ++maximum);
        }
        return candidate;
    }

    std::variant<std::string, AssetError> EncodeLocalIDManifest(const LocalIDManifest& manifest)
    {
        if (!manifest.Source.IsValid())
            return AssetError {AssetErrorCode::InvalidArgument, "Invalid LocalID manifest source"};
        auto validated = ValidateMap(manifest.IDs);
        if (const auto* error = std::get_if<AssetError>(&validated))
            return *error;
        YAML::Emitter out;
        out << YAML::BeginMap << YAML::Key << "version" << YAML::Value << 1;
        out << YAML::Key << "source_id" << YAML::Value << manifest.Source.ToString();
        out << YAML::Key << "entries" << YAML::Value << YAML::BeginSeq;
        for (const auto& entry : manifest.IDs)
            out << YAML::BeginMap << YAML::Key << "key" << YAML::Value << entry.first << YAML::Key << "local_id"
                << YAML::Value << entry.second << YAML::EndMap;
        out << YAML::EndSeq << YAML::EndMap;
        if (!out.good() || out.size() > kMaximumManifestBytes)
            return AssetError {AssetErrorCode::InvalidData, "Cannot encode LocalID manifest within size limit"};
        return std::string(out.c_str(), out.size());
    }

    std::variant<LocalIDManifest, AssetError> DecodeLocalIDManifest(const std::string& bytes,
                                                                    SourceAssetID      expectedSource)
    {
        if (!expectedSource.IsValid())
            return AssetError {AssetErrorCode::InvalidArgument, "Invalid expected manifest source"};
        if (bytes.empty() || bytes.size() > kMaximumManifestBytes)
            return AssetError {AssetErrorCode::InvalidData, "Invalid LocalID manifest size"};
        try
        {
            const auto root = YAML::Load(bytes);
            if (!root.IsMap() || root.size() != 3)
                return AssetError {AssetErrorCode::InvalidData, "Invalid LocalID manifest root"};
            if (!root["version"].IsScalar() || !root["source_id"].IsScalar() || !root["entries"].IsSequence())
                return AssetError {AssetErrorCode::InvalidData, "Invalid LocalID manifest fields"};
            if (root["version"].as<uint32_t>() != 1)
                return AssetError {AssetErrorCode::UnsupportedVersion, "Unsupported LocalID manifest version"};
            const auto id = root["source_id"].as<std::string>();
            if (id.size() != 16 || SourceAssetID::FromString(id) != expectedSource)
                return AssetError {AssetErrorCode::InvalidData, "LocalID manifest belongs to another source"};
            if (root["entries"].size() > kMaximumEntries)
                return AssetError {AssetErrorCode::InvalidData, "Too many LocalID manifest entries"};
            LocalIDManifest result {expectedSource, {}};
            for (const auto& entry : root["entries"])
            {
                if (!entry.IsMap() || entry.size() != 2 || !entry["key"].IsScalar() || !entry["local_id"].IsScalar())
                    return AssetError {AssetErrorCode::InvalidData, "Invalid LocalID manifest entry"};
                const auto key  = entry["key"].as<std::string>();
                const auto text = entry["local_id"].as<std::string>();
                // Reject signs and alternate representations instead of accepting
                // YAML's permissive conversion of negative numbers to unsigned.
                if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
                    return AssetError {AssetErrorCode::InvalidData, "Invalid LocalID decimal value"};
                if (!result.IDs.emplace(key, entry["local_id"].as<uint64_t>()).second)
                    return AssetError {AssetErrorCode::DuplicateID, "Duplicate LocalID object key"};
            }
            auto validated = ValidateMap(result.IDs);
            if (const auto* error = std::get_if<AssetError>(&validated))
                return *error;
            return result;
        }
        catch (const YAML::Exception& error)
        {
            return AssetError {AssetErrorCode::InvalidData,
                               "Cannot decode LocalID manifest: " + std::string(error.what())};
        }
    }
} // namespace Kans
