#pragma once
#include "Kans3D/RHI/RHI.h"
#include "Kans3D/Renderer/RHI/OpenGL/VertexArray.h"
#include "Kans3D/Renderer/Resource/Texture.h"

namespace Kans::RenderHardware::OpenGLInterop
{
    // Transitional context-local device lease. The final lease dies before the GL context.
    std::shared_ptr<Device> AcquireDevice();
    uint32_t                BufferName(const Handle<Buffer>& buffer);
    uint32_t                TextureName(const Handle<RenderHardware::Texture>& texture);
    uint32_t                ProgramName(const Handle<Shader>& shader);
    uint32_t                UniformPoint(const Handle<Shader>& shader, uint32_t group, uint32_t slot);
    uint32_t                TextureUnit(const Handle<Shader>& shader, uint32_t group, uint32_t slot);
    // Only for remaining immediate GL consumers; native resources remain RHI-owned.
    Ref<Kans::VertexArray>
    MeshVertexArray(std::shared_ptr<Device> device, Handle<Buffer> vertices, Handle<Buffer> indices, uint32_t count);
    Ref<Kans::Texture2D> TextureView(std::shared_ptr<Device> device, Handle<RenderHardware::Texture> texture);
    Handle<RenderHardware::Texture> TextureResource(const Ref<Kans::Texture2D>& texture);
    // Borrow an IBL texture owned by the legacy environment precomputation.
    Handle<RenderHardware::Texture>
         BorrowTexture(Device& device, uint32_t name, TextureDimension dimension, std::shared_ptr<void> owner);
    void BindTexture(const Handle<RenderHardware::Texture>& texture, uint32_t unit);
    void BindUniform(const Handle<Buffer>& buffer, uint32_t point);
    void BindProgram(const Handle<Shader>& shader);
    void SubmitUpload(Device& device, const Handle<CommandList>& commands);
    // Preserve caller-bound legacy resources while draining the RHI deletion queue.
    void                  RetireUnbound(Device& device);
    uint32_t              CreateSceneFramebuffer(const std::vector<Handle<RenderHardware::Texture>>& colors,
                                                 const Handle<RenderHardware::Texture>&              depth);
    void                  BindSceneFramebuffer(uint32_t framebuffer, uint32_t width, uint32_t height);
    std::vector<uint32_t> SaveSamplers();
    void                  RestoreSamplers(const std::vector<uint32_t>& samplers);
    void                  DeleteSceneFramebuffer(uint32_t framebuffer);
} // namespace Kans::RenderHardware::OpenGLInterop
