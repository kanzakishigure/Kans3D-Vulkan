#pragma once
#include <map>
#include <variant>
#include "Kans3D/Asset/Runtime/MaterialShaderInterface.h"
#include "Kans3D/RHI/RHI.h"

namespace Kans
{
    // RHI upload-buffer owner. Bind/GetRendererID are transitional preview shims.
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
        void Bind(uint32_t point) const;
        uint32_t                                              GetRendererID() const;
        const RenderHardware::Handle<RenderHardware::Buffer>& Resource() const { return m_Buffer; }

    private:
        std::shared_ptr<RenderHardware::Device>        m_Device;
        RenderHardware::Handle<RenderHardware::Buffer> m_Buffer;
    };

    // Validated material product backed by an RHI Shader and cached state pipelines.
    // Native Bind/point/unit access is retained for the immediate preview during phase 1.
    class GpuShader
    {
        std::shared_ptr<RenderHardware::Device> m_Device;

    public:
        static constexpr uint32_t FrameBlockPoint        = 0;
        static constexpr uint32_t ObjectBlockPoint       = 1;
        static constexpr uint32_t IrradianceUnit         = 0;
        static constexpr uint32_t PrefilterUnit          = 1;
        static constexpr uint32_t BrdfLUTUnit            = 2;
        static constexpr uint32_t FirstMaterialBlockPoint = 2;
        static constexpr uint32_t FirstMaterialUnit       = 3;

        // Requires a current GL context. Validates the material contract.
        [[nodiscard]] static std::variant<GpuShader, AssetError>
        Create(const ShaderData& data, std::shared_ptr<RenderHardware::Device> device = {});

        ~GpuShader();
        GpuShader(GpuShader&& other) noexcept;
        GpuShader& operator=(GpuShader&& other) noexcept;
        GpuShader(const GpuShader&)            = delete;
        GpuShader& operator=(const GpuShader&) = delete;

        void Bind() const;
        uint32_t                                                                     GetRendererID() const;
        RenderHardware::Handle<RenderHardware::Shader>                               Program;
        RenderHardware::Handle<RenderHardware::BindGroupLayout>                      EngineLayout, MaterialLayout;
        std::vector<ShaderResourceBinding>                                           EngineResources;
        mutable std::map<uint32_t, RenderHardware::Handle<RenderHardware::Pipeline>> Pipelines;

        // Set-1 binding to GL uniform block point / texture unit.
        const std::map<uint32_t, uint32_t>& MaterialBlockPoints() const { return m_BlockPoints; }
        const std::map<uint32_t, uint32_t>& MaterialSamplerUnits() const { return m_SamplerUnits; }

        const std::map<uint32_t, ShaderTextureFallback>& MaterialSamplerFallbacks() const { return m_SamplerFallbacks; }

    private:
        GpuShader() = default;

        std::map<uint32_t, ShaderTextureFallback> m_SamplerFallbacks;
        std::map<uint32_t, uint32_t> m_BlockPoints;
        std::map<uint32_t, uint32_t> m_SamplerUnits;
    };
} // namespace Kans
