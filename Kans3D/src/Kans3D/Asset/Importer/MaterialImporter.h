#pragma once
#include "AssetImporterBase.h"
#include "MaterialSource.h"
namespace Kans
{
    // Imports a `.material` source (MaterialSource.h) to a KMAT Product.
    class MaterialImporter final : public AssetImporterBase
    {
    public:
        uint32_t                               GetVersion() const override { return 6; }
        bool                                   RequiresProductBytes() const override { return true; }
        std::variant<ImportInputs, AssetError> DiscoverInputs(const ImportContext& context) const override;
        std::variant<ImportResult, AssetError> Import(const ImportContext& context) const override;
    };
} // namespace Kans
