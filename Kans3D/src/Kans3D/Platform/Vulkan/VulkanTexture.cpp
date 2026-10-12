#include "kspch.h"
#include "VulkanTexture.h"

namespace Kans
{

    VulkanTexture2D::VulkanTexture2D(const TextureSpecification& specification, const std::filesystem::path& filepath) :
        m_Specification(specification)
    {}

    VulkanTexture2D::VulkanTexture2D(const TextureSpecification& specification, Buffer data /*= Buffer()*/) :
        m_Specification(specification), m_Buffer(data)
    {}

    VulkanTexture2D::~VulkanTexture2D() {}

    uintptr_t VulkanTexture2D::GetImGuiTextureID() const { return 0; }

    const std::filesystem::path& VulkanTexture2D::GetPath() const { return m_Path; }

} // namespace Kans