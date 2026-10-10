#pragma once
#include <map>
#include <variant>
#include "Kans3D/Asset/Runtime/MaterialShaderInterface.h"

namespace Kans
{
    // OpenGL uniform buffer. Requires a current GL context for its whole life.
    class GpuUniformBuffer
    {
    public:
        explicit GpuUniformBuffer(size_t bytes, const void* data = nullptr);
        ~GpuUniformBuffer();
        GpuUniformBuffer(GpuUniformBuffer&& other) noexcept;
        GpuUniformBuffer& operator=(GpuUniformBuffer&& other) noexcept;
        GpuUniformBuffer(const GpuUniformBuffer&)            = delete;
        GpuUniformBuffer& operator=(const GpuUniformBuffer&) = delete;

        void Update(const void* data, size_t bytes);
        void Bind(uint32_t point) const;

    private:
        uint32_t m_ID = 0;
    };

    // OpenGL program built from a material Shader Product (SPIR-V for Vulkan),
    // cross-compiled to GLSL 4.30. Descriptors are flattened to GL binding points:
    //   uniform blocks - engine Frame 0, Object 1, material blocks from 2;
    //   texture units  - engine IrradianceMap 0, PrefilterMap 1, BrdfLUT 2, material samplers from 3.
    class GpuShader
    {
    public:
        static constexpr uint32_t FrameBlockPoint        = 0;
        static constexpr uint32_t ObjectBlockPoint       = 1;
        static constexpr uint32_t IrradianceUnit         = 0;
        static constexpr uint32_t PrefilterUnit          = 1;
        static constexpr uint32_t BrdfLUTUnit            = 2;
        static constexpr uint32_t FirstMaterialBlockPoint = 2;
        static constexpr uint32_t FirstMaterialUnit       = 3;

        // Requires a current GL context. Validates the material contract.
        [[nodiscard]] static std::variant<GpuShader, AssetError> Create(const ShaderData& data);

        ~GpuShader();
        GpuShader(GpuShader&& other) noexcept;
        GpuShader& operator=(GpuShader&& other) noexcept;
        GpuShader(const GpuShader&)            = delete;
        GpuShader& operator=(const GpuShader&) = delete;

        void Bind() const;

        // Set-1 binding to GL uniform block point / texture unit.
        const std::map<uint32_t, uint32_t>& MaterialBlockPoints() const { return m_BlockPoints; }
        const std::map<uint32_t, uint32_t>& MaterialSamplerUnits() const { return m_SamplerUnits; }

        const std::map<uint32_t, ShaderTextureFallback>& MaterialSamplerFallbacks() const { return m_SamplerFallbacks; }

    private:
        GpuShader() = default;

        std::map<uint32_t, ShaderTextureFallback> m_SamplerFallbacks;
        uint32_t                     m_Program = 0;
        std::map<uint32_t, uint32_t> m_BlockPoints;
        std::map<uint32_t, uint32_t> m_SamplerUnits;
    };
} // namespace Kans
