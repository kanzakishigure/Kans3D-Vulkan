#include "kspch.h"
#include "ProductModelComponent.h"
#include <unordered_set>

namespace Kans
{
    std::variant<std::monostate, AssetError> ValidateProductModelComponent(const ProductModelComponent& value)
    {
        // Unassigned references are allowed while authoring and are skipped when drawing.
        std::unordered_set<AssetID> authored;
        for (const auto& item : value.Overrides)
            if (item.Authored.IsValid() && !authored.insert(item.Authored).second)
                return AssetError {AssetErrorCode::InvalidArgument, "Duplicate Material override"};
        return std::monostate {};
    }
} // namespace Kans
