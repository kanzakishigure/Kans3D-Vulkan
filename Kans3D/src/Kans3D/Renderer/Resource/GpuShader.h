#pragma once
#include <map>
#include <variant>
#include "Kans3D/Asset/Runtime/MaterialShaderInterface.h"
#include "Kans3D/RHI/RHI.h"

namespace Kans
{
    // RHI upload-buffer owner.
    class GpuUniformBuffer
    {
    public:
        explicit GpuUniformBuffer(size_t                                  bytes,
                                  const void*                             data   = nullptr,
                                  std::shared_ptr<RenderHardware::Device> device = {});
        ~GpuUniformBuffer();
        GpuUniformBuffer(GpuUniformBuffer&& other) noexcept;
        GpuUniformBuffer& operator=(GpuUniformBuffer&& other) noexcept;
        GpuUniformBuffer(const GpuUniformBuffer&)            = delete;
        GpuUniformBuffer& operator=(const GpuUniformBuffer&) = delete;

        void Update(const void* data, size_t bytes);
        const RenderHardware::Handle<RenderHardware::Buffer>& Resource() const { return m_Buffer; }

    private:
        std::shared_ptr<RenderHardware::Device>        m_Device;
        RenderHardware::Handle<RenderHardware::Buffer> m_Buffer;
    };

    struct GpuShaderPass
    {
        std::string                                                                  Name, Tag;
        ShaderPassState                                                              State;
        RenderHardware::Handle<RenderHardware::Shader>                               Program;
        mutable std::map<uint32_t, RenderHardware::Handle<RenderHardware::Pipeline>> Pipelines;
    };
    // Validated material product backed by an RHI Shader and cached state pipelines.
    class GpuShader
    {
        std::shared_ptr<RenderHardware::Device> m_Device;

    public:
        // Validates the material contract; the owning device must be current.
        // Omitting the device is a transitional current-context construction path.
        [[nodiscard]] static std::variant<GpuShader, AssetError>
        Create(const ShaderData& data, std::shared_ptr<RenderHardware::Device> device = {});

        ~GpuShader();
        GpuShader(GpuShader&& other) noexcept;
        GpuShader& operator=(GpuShader&& other) noexcept;
        GpuShader(const GpuShader&)            = delete;
        GpuShader& operator=(const GpuShader&) = delete;

        RenderHardware::Handle<RenderHardware::Shader>                               Program;
        RenderHardware::Handle<RenderHardware::BindGroupLayout>                      EngineLayout, MaterialLayout;
        std::vector<ShaderResourceBinding>                                           EngineResources;
        mutable std::map<uint32_t, RenderHardware::Handle<RenderHardware::Pipeline>> Pipelines;

        std::vector<GpuShaderPass> Passes;
        ShaderOutputSpace          OutputSpace = ShaderOutputSpace::Display;

        const std::map<uint32_t, ShaderTextureFallback>& MaterialSamplerFallbacks() const { return m_SamplerFallbacks; }

    private:
        GpuShader() = default;

        std::map<uint32_t, ShaderTextureFallback> m_SamplerFallbacks;
    };
} // namespace Kans
