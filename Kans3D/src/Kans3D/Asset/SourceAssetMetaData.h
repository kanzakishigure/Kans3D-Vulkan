#pragma once

#include <cstdint>
#include <filesystem>
#include "Asset.h"
#include "Importer/LocalIDMap.h"
#include "SourceImportConfiguration.h"

namespace Kans
{
    class SourceAssetMetadata
    {
    public:
        SourceAssetMetadata()  = default;
        ~SourceAssetMetadata() = default;
        bool IsValid() const noexcept { return sourceAssetID.IsValid() && Path.IsValid() && Import.IsValid(); }

    public:
        SourceAssetID             sourceAssetID;
        AssetPath                 Path;
        uint32_t                  MetaVersion = 3;
        SourceImportConfiguration Import;
        // Object key -> LocalID of every object imported from this source. This is
        // identity, not cache: AssetIDs of the source's Products depend on it.
        LocalIDMap LocalIDs;

        bool Exists  = false;
        bool Missing = false;

        uint64_t                        FileSize = 0;
        std::filesystem::file_time_type last_edit_time {};
    };
} // namespace Kans
