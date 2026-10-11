#pragma once
#include "Kans3D/RHI/RHI.h"
#include "Kans3D/Renderer/Resource/Texture.h"

namespace Kans::RenderHardware::OpenGLInterop
{
    // Transitional context-local device lease. The final lease dies before the GL context.
    std::shared_ptr<Device> AcquireDevice();
    uint32_t                BufferName(const Handle<Buffer>& buffer);
    uint32_t                TextureName(const Handle<RenderHardware::Texture>& texture);
    // Native shader probes used by regression tests, not by renderer consumers.
    uint32_t                        PipelineVertexArrayName(const Handle<Pipeline>& pipeline);
    uint32_t                ProgramName(const Handle<Shader>& shader);
    uint32_t                UniformPoint(const Handle<Shader>& shader, uint32_t group, uint32_t slot);
    uint32_t                TextureUnit(const Handle<Shader>& shader, uint32_t group, uint32_t slot);
    Ref<Kans::Texture2D> TextureView(std::shared_ptr<Device> device, Handle<RenderHardware::Texture> texture);
    Handle<RenderHardware::Texture> TextureResource(const Ref<Kans::Texture2D>& texture);
    SamplerDesc                     TextureSampling(const Ref<Kans::Texture2D>& texture);
    void BindTexture(const Handle<RenderHardware::Texture>& texture, uint32_t unit);
} // namespace Kans::RenderHardware::OpenGLInterop
