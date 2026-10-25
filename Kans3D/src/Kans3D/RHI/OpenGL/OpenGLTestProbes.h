#pragma once
#include "Kans3D/RHI/RHI.h"
namespace Kans::RenderHardware::OpenGLTestProbes
{
    uint32_t BufferName(const Handle<Buffer>& buffer);
    uint32_t TextureName(const Handle<RenderHardware::Texture>& texture);
    // Native shader probes used by regression tests, not by renderer consumers.
    uint32_t PipelineVertexArrayName(const Handle<Pipeline>& pipeline);
    uint32_t ProgramName(const Handle<Shader>& shader);
    uint32_t UniformPoint(const Handle<Shader>& shader, uint32_t group, uint32_t slot);
    uint32_t TextureUnit(const Handle<Shader>& shader, uint32_t group, uint32_t slot);
    void     BindTexture(const Handle<Texture>& texture, uint32_t unit);
} // namespace Kans::RenderHardware::OpenGLTestProbes
