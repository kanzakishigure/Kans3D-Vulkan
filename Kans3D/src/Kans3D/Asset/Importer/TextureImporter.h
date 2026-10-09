#pragma once
#include "AssetImporterBase.h"
namespace Kans
{
    class TextureImporter final : public AssetImporterBase
    {
    public:
        uint32_t                                             GetVersion() const override { return 2; }
        [[nodiscard]] std::variant<ImportResult, AssetError> Import(const ImportContext& context) const override;
    };
} // namespace Kans
