#pragma once
#include <map>
#include <vector>
#include "Kans3D/Asset/Asset.h"

namespace Kans
{
    class ImportContext
    {
    public:
        SourceAssetID         SourceID;
        std::filesystem::path SourceFile;
        // Exclusive temporary output directory supplied by the pipeline coordinator.
        std::filesystem::path              StagingDirectory;
        std::map<std::string, std::string> Settings;
        // Persistent source-object key -> LocalID. Never derive IDs from array order.
        std::map<std::string, uint64_t> PreviousLocalIDs;
    };

    class ProductDescriptor
    {
    public:
        uint64_t  LocalID = 0;
        AssetType Type    = AssetType::None;
        // Relative to StagingDirectory, not a source path or final ProductLocation.
        std::filesystem::path StagedFile;
        std::vector<AssetID>  Dependencies;
    };

    class ImportResult
    {
    public:
        // Complete product set, including an empty set if the source emits nothing.
        std::vector<ProductDescriptor>  Products;
        std::map<std::string, uint64_t> LocalIDs;
        std::vector<SourceAssetID>      SourceDependencies;
    };
} // namespace Kans
