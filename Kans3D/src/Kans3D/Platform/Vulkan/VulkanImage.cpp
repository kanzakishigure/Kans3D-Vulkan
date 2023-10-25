#include "kspch.h"
#include "VulkanImage.h"
namespace Kans
{

	VulkanImage2D::VulkanImage2D(ImageSpecification spec)
	{

	}

	VulkanImage2D::~VulkanImage2D()
	{

	}

	void VulkanImage2D::Resize(uint32_t width, uint32_t height)
	{
		m_ImageSpecification.Width	= width;
		m_ImageSpecification.Height = height;
		throw std::logic_error("The method or operation is not implemented.");
	}

	void VulkanImage2D::Release()
	{
		throw std::logic_error("The method or operation is not implemented.");
	}

	void VulkanImage2D::Invalidate()
	{
		throw std::logic_error("The method or operation is not implemented.");
	}
}
