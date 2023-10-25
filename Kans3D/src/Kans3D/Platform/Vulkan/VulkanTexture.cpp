#include "kspch.h"
#include "VulkanTexture.h"

namespace Kans
{

	VulkanTexture2D::VulkanTexture2D(const TextureSpecification& specification, const std::filesystem::path& filepath)
		:m_Specification(specification)
	{

	}

	VulkanTexture2D::VulkanTexture2D(const TextureSpecification& specification, Buffer data /*= Buffer()*/)
		:m_Specification(specification),m_Buffer(data)
	{

	}

	VulkanTexture2D::~VulkanTexture2D()
	{

	}

	


	uint32_t VulkanTexture2D::GetRenererID() const
	{
		return 0;
	}

	

	void VulkanTexture2D::Bind(uint32_t slot /*= 0*/) const
	{
		
	}

	const std::filesystem::path& VulkanTexture2D::GetPath() const
	{
		return m_Path;
	}

}