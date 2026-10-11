#include "kspch.h"
#include "MaterialPass.h"

namespace Kans
{
    bool RecordMaterialDraw(RenderAssetCache&            cache,
                            RenderHardware::CommandList& commands,
                            const GpuShader&             shader,
                            const GpuMaterialBindings&   bindings,
                            const GpuMesh&               mesh,
                            const MaterialPassData&      data)
    {
        using namespace RenderHardware;
        auto       device      = cache.GetDevice();
        const bool transparent = data.Render.AlphaMode == MaterialAlphaMode::Blend;
        // Both clients use RGBA8 and D24S8. The attachment count differentiates scene/preview.
        const uint32_t key      = uint32_t(transparent) | (uint32_t(data.Render.DoubleSided) << 1) |
                                  (uint32_t(data.Wireframe) << 2) | (uint32_t(data.WriteStencil) << 3) |
                                  (uint32_t(data.ColorFormats.size()) << 8);
        auto&          pipeline = shader.Pipelines[key];
        if (!pipeline)
        {
            PipelineDesc desc;
            desc.Program                      = shader.Program;
            desc.GroupLayouts                 = {shader.EngineLayout, shader.MaterialLayout};
            desc.VertexBindings               = mesh.VertexBindings;
            desc.Attributes                   = mesh.Attributes;
            desc.ColorFormats                 = data.ColorFormats;
            desc.HasDepthStencil              = true;
            desc.DepthStencilFormat           = Format::Depth24Stencil8;
            desc.DepthStencil.DepthTest       = true;
            desc.DepthStencil.DepthWrite      = !transparent;
            desc.DepthStencil.StencilTest     = data.WriteStencil;
            desc.DepthStencil.Front.Reference = desc.DepthStencil.Back.Reference = 1;
            desc.DepthStencil.Front.Pass = desc.DepthStencil.Back.Pass = StencilOp::Replace;
            desc.Rasterizer.Front   = device->GetCoordinateConventions().ClipY == ClipYDirection::Up ?
                                          FrontFace::CounterClockwise :
                                          FrontFace::Clockwise;
            desc.Rasterizer.Cull    = data.Render.DoubleSided ? CullMode::None : CullMode::Back;
            desc.Rasterizer.Polygon = data.Wireframe ? PolygonMode::Line : PolygonMode::Fill;
            BlendState blend;
            blend.Enabled          = transparent;
            blend.WriteMask        = 7;
            blend.SourceColor      = BlendFactor::SourceAlpha;
            blend.DestinationColor = BlendFactor::OneMinusSourceAlpha;
            blend.SourceAlpha      = BlendFactor::Zero;
            blend.DestinationAlpha = BlendFactor::One;
            desc.Blends.resize(data.ColorFormats.size(), blend);
            pipeline = device->CreatePipeline(desc);
        }
        BindGroupDesc engine;
        engine.Layout = shader.EngineLayout;
        for (const auto& binding : shader.EngineResources)
        {
            BindGroupEntry entry;
            entry.Slot = binding.Binding;
            if (binding.Binding == uint32_t(EngineBinding::Frame))
            {
                entry.BufferResource = data.Frame;
                entry.Offset         = data.FrameOffset;
                entry.Bytes          = sizeof(EngineFrameUniforms);
            }
            else if (binding.Binding == uint32_t(EngineBinding::Object))
            {
                entry.BufferResource = data.Object;
                entry.Offset         = data.ObjectOffset;
                entry.Bytes          = sizeof(EngineObjectUniforms);
            }
            else
            {
                entry.TextureResource = data.Environment.at(binding.Binding - 2);
                entry.SamplerResource = data.EnvironmentSampler;
            }
            engine.Entries.push_back(std::move(entry));
        }
        bool complete = true;
        auto material = cache.GetMaterialGroup(shader, bindings, &complete);
        if (!material)
            return false;
        commands.BindPipeline(pipeline);
        commands.BindGroup(device->CreateBindGroup(engine));
        commands.BindGroup(material);
        commands.BindVertexBuffers({{0, mesh.Vertices, 0}});
        commands.BindIndexBuffer(mesh.Indices, IndexType::Uint32);
        commands.DrawIndexed(mesh.IndexCount);
        return complete;
    }
} // namespace Kans
