#pragma once
#include "ModelData.h"
#include "ShaderData.h"

namespace Kans
{
    // Reserved engine identity, never a project dependency or file.
    [[nodiscard]] AssetID StandardShaderID();
    [[nodiscard]] bool    IsStandardShader(AssetID id);
    // Compiled and reflected once on the CPU; requires no graphics context.
    [[nodiscard]] const ShaderData&          GetStandardShader();
    [[nodiscard]] Ref<const ShaderDataAsset> GetStandardShaderAsset();
    // Adapts pre-recipe KMAT materials and imported semantic texture bindings.
    [[nodiscard]] std::variant<MaterialData, AssetError>
    ResolveStandardMaterial(const MaterialData& legacy, std::optional<std::array<float, 3>> emissive = std::nullopt);
} // namespace Kans
