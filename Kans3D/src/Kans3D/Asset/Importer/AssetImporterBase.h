#pragma once
#include <variant>
#include "Kans3D/Asset/AssetError.h"
#include "ImportContext.h"

namespace Kans
{
    class AssetImporterBase
    {
    public:
        virtual ~AssetImporterBase()        = default;
        virtual uint32_t GetVersion() const = 0;
        // Source -> complete staged Product set. No runtime Asset/GPU construction.
        // Implementations must preserve LocalIDs and never modify the live registry.
        [[nodiscard]] virtual std::variant<ImportResult, AssetError> Import(const ImportContext& context) const = 0;
    };
} // namespace Kans
