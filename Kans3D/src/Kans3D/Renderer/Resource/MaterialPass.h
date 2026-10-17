#pragma once
#include "RenderAssetCache.h"

namespace Kans
{
    struct MaterialPassData
    {
        RenderHardware::Handle<RenderHardware::Buffer>                 Frame, Object;
        size_t                                                         FrameOffset = 0, ObjectOffset = 0;
        std::array<RenderHardware::Handle<RenderHardware::Texture>, 3> Environment;
        RenderHardware::Handle<RenderHardware::Sampler>                EnvironmentSampler;
        std::vector<RenderHardware::Format> ColorFormats {RenderHardware::Format::RGBA8Unorm};
        bool                                Wireframe = false, WriteStencil = false;
        MaterialRenderState                 Render;
        uint32_t                                                       Section = 0;
    };
    // Shared immutable pipeline/engine group/material group binding for scene and previews.
    // Returns false when assigned textures are unavailable; fallback textures still draw.
    bool RecordMaterialDraw(RenderAssetCache&            cache,
                            RenderHardware::CommandList& commands,
                            const GpuShader&             shader,
                            const GpuMaterialBindings&   bindings,
                            const GpuMesh&               mesh,
                            const MaterialPassData&      data,
                            const GpuShaderPass*         extraPass = nullptr);
} // namespace Kans
