#pragma once
#include "Kans3D/Renderer/RHI/Vulkan/Image.h"

namespace Kans
{
	class VulkanImage2D : public Image2D
	{
	public:
		VulkanImage2D(ImageSpecification spec);
		~VulkanImage2D();

		void Resize(uint32_t width, uint32_t height) override;
		void Release() override;
		void Invalidate() override;


		uint32_t GetWidth() const override { return m_ImageSpecification.Width; }
		uint32_t GetHeight() const override { return m_ImageSpecification.Height; };
		glm::uvec2 GetSize() const override { return {m_ImageSpecification.Width,m_ImageSpecification.Height}; }

		const ImageSpecification& GetSpecification() const override { return m_ImageSpecification; }
		ImageSpecification& GetSpecification() override { return m_ImageSpecification; }

	private:
		ImageSpecification m_ImageSpecification;
	};
}