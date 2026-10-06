#pragma once
#include "Kans3D/Asset/AssetMetadata.h"

namespace Kans
{
    struct ProductGeneration
    {
        AssetPath Directory;
    };
    struct ProductLocation
    {
        AssetPath   RelativeFile;
        uint64_t    ByteSize = 0;
        std::string ContentDigest;
        // In-process lease; not serialized. Store GC honors old snapshot readers.
        Ref<const ProductGeneration> Generation;
    };
    struct ProductRecord
    {
        AssetMetadata   Metadata;
        std::string     Target = "default";
        ProductLocation Location;
    };
    bool IsProductTarget(const std::string& target);
} // namespace Kans
