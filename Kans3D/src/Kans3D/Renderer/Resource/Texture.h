#pragma once
#include <filesystem>
#include "Kans3D/Asset/Runtime/TextureData.h"
#include "Kans3D/Core/ByteBuffer.h"
#include "Kans3D/RHI/RHI.h"
namespace Kans
{
    struct TextureSpecification
    {
        uint32_t                    Width = 0, Height = 0;
        RenderHardware::Format      Format = RenderHardware::Format::RGBA8Unorm;
        RenderHardware::Filter      Minf = RenderHardware::Filter::Linear, Maxf = RenderHardware::Filter::Nearest;
        RenderHardware::AddressMode Wrap         = RenderHardware::AddressMode::Repeat;
        bool                        GenerateMips = false, SRGB = false;
    };
    class Texture : public Asset
    {
    public:
        virtual uint32_t                                  GetHeight() const         = 0;
        virtual uint32_t                                  GetWidth() const          = 0;
        virtual uintptr_t                                 GetImGuiTextureID() const = 0;
        virtual const std::filesystem::path&              GetPath() const = 0;
    };
    class Texture2D : public Texture
    {
    public:
        Texture2D(std::shared_ptr<RenderHardware::Device>         device,
                  RenderHardware::Handle<RenderHardware::Texture> image,
                  const RenderHardware::SamplerDesc&              sampling = {},
                  std::filesystem::path                           path     = {});
        uint32_t                     GetHeight() const override { return m_Image->GetDesc().Height; }
        uint32_t                     GetWidth() const override { return m_Image->GetDesc().Width; }
        uintptr_t                    GetImGuiTextureID() const override;
        const std::filesystem::path& GetPath() const override { return m_Path; }
        const RenderHardware::Handle<RenderHardware::Texture>& GetResource() const { return m_Image; }
        const RenderHardware::SamplerDesc&                     GetSampling() const { return m_Sampling; }
        const std::shared_ptr<RenderHardware::Device>&         GetDevice() const { return m_Device; }
        [[nodiscard]] static std::variant<Ref<Texture2D>, AssetError>
        CreateFromProduct(const TextureData& data, std::shared_ptr<RenderHardware::Device> device = {});
        static Ref<Texture2D> Create(const TextureSpecification& specification, const std::filesystem::path& filepath);
        static Ref<Texture2D> Create(const TextureSpecification& specification, Buffer buffer = Buffer());

    private:
        std::shared_ptr<RenderHardware::Device>         m_Device;
        RenderHardware::Handle<RenderHardware::Texture> m_Image;
        RenderHardware::SamplerDesc                     m_Sampling;
        std::filesystem::path                           m_Path;
    };
} // namespace Kans
