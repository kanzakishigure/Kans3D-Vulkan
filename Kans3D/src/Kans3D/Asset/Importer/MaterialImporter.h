#pragma once
#include "Kans3D/Asset/Runtime/ModelData.h"
#include "AssetImporterBase.h"
namespace Kans
{
    // Tool-only human-authored source format; runtime reads KMAT instead.
    std::variant<MaterialData, AssetError> ParseMaterialSource(const std::byte* bytes, size_t size);
    class MaterialImporter                 final : public AssetImporterBase
    {
    public:
        uint32_t                               GetVersion() const override { return 3; }
        bool                                   RequiresProductBytes() const override { return true; }
        std::variant<ImportInputs, AssetError> DiscoverInputs(const ImportContext& context) const override;
        std::variant<ImportResult, AssetError> Import(const ImportContext& context) const override;
    };
} // namespace Kans
