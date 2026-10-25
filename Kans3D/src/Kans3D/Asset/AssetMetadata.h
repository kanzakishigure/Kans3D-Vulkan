#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>
#include "Kans3D/Asset/Asset.h"
namespace Kans
{
    class AssetMetadata
    {
    public:
        AssetMetadata() {}
        ~AssetMetadata() = default;
        bool IsValid() const { return assetID.IsValid() && Type != AssetType::None; }

    public:
        AssetType            Type = AssetType::None;
        AssetID              assetID;
        std::vector<AssetID> Dependencies;
    };

} // namespace Kans
