#pragma once
#include "Kans3D/Asset/Runtime/MeshData.h"

namespace Kans
{
    // A separate identity domain, never encoded as an AssetID or serialized.
    struct RuntimeMesh
    {
        explicit RuntimeMesh(MeshData data);
        RuntimeMesh(const RuntimeMesh&)                          = delete;
        RuntimeMesh&               operator=(const RuntimeMesh&) = delete;
        const uint64_t             Identity;
        const Ref<const MeshAsset> Source;
    };
    struct MeshKey
    {
        AssetID                Asset;
        Ref<const RuntimeMesh> Runtime;
        MeshKey() = default;
        MeshKey(AssetID asset) : Asset(asset) {}
        MeshKey(SourceAssetID source, uint64_t local) : Asset(source, local) {}
        MeshKey(Ref<const RuntimeMesh> runtime) : Runtime(std::move(runtime)) {}
        bool IsValid() const { return Runtime || Asset.IsValid(); }
        bool operator==(const MeshKey& b) const
        {
            return Runtime || b.Runtime ? Runtime && b.Runtime && Runtime->Identity == b.Runtime->Identity :
                                          Asset == b.Asset;
        }
        bool operator!=(const MeshKey& b) const { return !(*this == b); }
        bool operator<(const MeshKey& b) const
        {
            if (bool(Runtime) != bool(b.Runtime))
                return bool(Runtime) < bool(b.Runtime);
            return Runtime ? Runtime->Identity < b.Runtime->Identity : Asset < b.Asset;
        }
    };
} // namespace Kans
