#pragma once
#include "Kans3D/Renderer/Resource/Texture.h"

namespace Kans
{
    class VulkanTexture2D : public Texture
    {
    public:
        VulkanTexture2D(const TextureSpecification& specification, const std::filesystem::path& filepath);
        VulkanTexture2D(const TextureSpecification& specification, Buffer data = Buffer());
        ~VulkanTexture2D() override;

        virtual uint32_t GetWidth() const override { return m_Specification.Width; }
        virtual uint32_t GetHeight() const override { return m_Specification.Height; }

        const std::filesystem::path& GetPath() const override;

        uintptr_t GetImGuiTextureID() const override;

    private:
        std::filesystem::path m_Path;
        TextureSpecification  m_Specification;
        Buffer                m_Buffer;
    };

} // namespace Kans