#pragma once

#include <map>
#include <variant>
#include <vector>
#include "Kans3D/Asset/Asset.h"
#include "Kans3D/Asset/AssetError.h"

namespace Kans
{
    using LocalIDMap = std::map<std::string, uint64_t>;

    // Deleted keys remain reserved. The returned candidate is committed together
    // with its Product set; constructing it never changes the previous mapping.
    [[nodiscard]] std::variant<LocalIDMap, AssetError> AllocateLocalIDs(const LocalIDMap&               previous,
                                                                        const std::vector<std::string>& objectKeys);

    struct LocalIDManifest
    {
        SourceAssetID Source;
        LocalIDMap    IDs;
    };

    // Persistence codec only. The publication coordinator owns file IO/commit.
    [[nodiscard]] std::variant<std::string, AssetError>     EncodeLocalIDManifest(const LocalIDManifest& manifest);
    [[nodiscard]] std::variant<LocalIDManifest, AssetError> DecodeLocalIDManifest(const std::string& bytes,
                                                                                  SourceAssetID      expectedSource);
} // namespace Kans
