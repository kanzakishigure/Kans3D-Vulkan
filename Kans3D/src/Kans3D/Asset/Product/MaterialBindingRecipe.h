#pragma once
#include "Kans3D/Asset/Runtime/MaterialBindingRecipeData.h"
namespace Kans
{
    // Versioned canonical bytes for explicit binding assignments, independent of GPU layouts.
    std::variant<std::vector<std::byte>, AssetError> EncodeMaterialBindingRecipe(const MaterialBindingRecipe& recipe);
    std::variant<MaterialBindingRecipe, AssetError>  DecodeMaterialBindingRecipe(const std::byte* bytes, size_t size);
} // namespace Kans
