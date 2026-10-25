#pragma once
#include <optional>
#include <string>
#include <vector>
#include "Kans3D/Asset/Asset.h"

namespace Kans
{
    struct InspectedMesh
    {
        std::string              Name, ObjectKey;
        uint64_t                 VertexCount = 0, TriangleCount = 0;
        bool                     Skinned          = false;
        uint32_t                 MorphTargetCount = 0;
        std::vector<std::string> MaterialSlots, MorphTargets;
    };
    struct InspectedMaterialSlot
    {
        std::string            Key, SourceName;
        std::optional<AssetID> SuggestedMaterial;
    };
    struct InspectedTextureReference
    {
        std::string           MaterialSlot, Reference;
        std::filesystem::path ResolvedPath; // Expected path when missing; empty for embedded references.
        bool                  Found = false, Embedded = false;
    };
    struct InspectedSkeleton
    {
        uint32_t    JointCount = 0;
        std::string RootName;
    };
    struct InspectedClip
    {
        std::string Name, ObjectKey;
        double      DurationSeconds = 0, FrameRate = 0;
        uint64_t    KeyCount = 0; // Sum of position/rotation/scale and morph weight keys.
    };
    struct InspectedTexture
    {
        uint32_t    Width = 0, Height = 0, Channels = 0, BitDepth = 0;
        std::string Format;
        bool        HasAlpha = false, LikelyNormalMap = false;
        std::string NormalMapHeuristic;
    };
    struct SourceInventory
    {
        std::vector<InspectedMesh>             Meshes;
        std::vector<InspectedMaterialSlot>     Materials;
        std::vector<InspectedTextureReference> Textures;
        std::optional<InspectedSkeleton>       Skeleton;
        std::vector<InspectedClip>             Clips;
        std::vector<std::string>               MorphTargets, Warnings;
        std::optional<InspectedTexture>        Texture;
    };
} // namespace Kans
