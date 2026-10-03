#pragma once

#include <cstdint>
#include <filesystem>
#include "Asset.h"

namespace Kans
{
    class SourceAssetMetadata
    {
    public:
        SourceAssetMetadata()  = default;
        ~SourceAssetMetadata() = default;
        bool IsValid() const noexcept { return sourceAssetID.IsValid() && Path.IsValid(); }

    public:
        SourceAssetID sourceAssetID;
        AssetPath     Path;
        uint32_t      MetaVersion = 1;

        bool Exists  = false;
        bool Missing = false;

        uint64_t                        FileSize = 0;
        std::filesystem::file_time_type last_edit_time {};
    };
} // namespace Kans
