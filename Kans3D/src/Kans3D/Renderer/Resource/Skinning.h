#pragma once
#include "Kans3D/Asset/Runtime/ShaderData.h"
#include "Kans3D/Core/Math/Bounds.h"
#include "Kans3D/RHI/RHI.h"
namespace Kans
{
    struct SkinningPose
    {
        std::vector<glm::mat4> Palette;
        uint64_t               Version = 0;
    };
    // Material vertex inputs at locations 0/1/4 are transformed before the original main.
    // The static shader's bytecode is untouched. Set 2/binding 0 is a readonly mat4 palette SSBO;
    // skin stream locations 5/6 are uvec4 joints/vec4 weights. There is no fixed palette length.
    std::variant<ShaderData, AssetError>                    MakeSkinnedShader(const ShaderData&);
    RenderHardware::Handle<RenderHardware::BindGroupLayout> SkinningLayout(RenderHardware::Device&);
    void   BindSkinning(RenderHardware::Device&,
                        RenderHardware::CommandList&,
                        const RenderHardware::Handle<RenderHardware::BindGroupLayout>&,
                        const RenderHardware::Handle<RenderHardware::Buffer>&);
    Bounds SkinnedBounds(const std::vector<Bounds>& jointBounds, const SkinningPose&);
} // namespace Kans
