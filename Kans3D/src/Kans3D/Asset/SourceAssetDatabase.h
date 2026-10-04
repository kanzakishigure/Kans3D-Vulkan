
#pragma once
#include <map>
#include <unordered_map>
#include <variant>
#include <vector>
#include "Kans3D/Asset/SourceAssetMetaData.h"
#include "Asset.h"
#include "AssetError.h"
namespace Kans
{

    class SourceAssetDatabase
    {
    public:
        SourceAssetDatabase()                                      = default;
        ~SourceAssetDatabase()                                     = default;
        SourceAssetDatabase(const SourceAssetDatabase&)            = default;
        SourceAssetDatabase& operator=(const SourceAssetDatabase&) = default;
        // Publishing a fully validated snapshot must not allocate or partially copy indexes.
        SourceAssetDatabase(SourceAssetDatabase&&) noexcept            = default;
        SourceAssetDatabase& operator=(SourceAssetDatabase&&) noexcept = default;
        // Queries return copies; an absent entry returns an invalid value.
        SourceAssetMetadata FindByID(SourceAssetID sourceAssetID) const;
        SourceAssetID       FindIDByPath(const AssetPath& path) const;
        bool                Contains(SourceAssetID sourceAssetID) const;
        bool                Contains(const AssetPath& path) const;
        // Snapshot for persistence and tooling; callers cannot mutate internal
        // records.
        std::vector<SourceAssetMetadata> GetAllSources() const;
        // Registration never replaces an existing ID or path, even for the same
        // record.
        std::variant<std::monostate, AssetError> RegisterSource(const SourceAssetMetadata& data);

        // Record-only mutations: never touch source files, sidecars or Products.
        // Update requires an existing ID and an unchanged path. Missing records
        // retain their path reservation until explicitly unregistered or moved.
        [[nodiscard]] std::variant<std::monostate, AssetError> UpdateSource(const SourceAssetMetadata& data);
        [[nodiscard]] std::variant<std::monostate, AssetError> UnregisterSource(SourceAssetID id);
        [[nodiscard]] std::variant<std::monostate, AssetError> ChangeSourcePath(SourceAssetID    id,
                                                                                const AssetPath& path);

    private:
        std::unordered_map<SourceAssetID, SourceAssetMetadata> m_DataBase;
        std::unordered_map<AssetPath, SourceAssetID>           m_AssetPaths;
    };
} // namespace Kans
