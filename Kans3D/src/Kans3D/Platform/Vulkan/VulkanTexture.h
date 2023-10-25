#pragma once
#include "Kans3D/Renderer/Resource/Texture.h"

namespace Kans
{
	class VulkanTexture2D :public Texture2D
	{
	public:
		VulkanTexture2D(const TextureSpecification& specification, const std::filesystem::path& filepath);
		VulkanTexture2D(const TextureSpecification& specification, Buffer data = Buffer());
		~VulkanTexture2D() override;

		virtual uint32_t GetWidth() const override { return m_Specification.Width; }
		virtual uint32_t GetHeight() const override { return m_Specification.Height; }
		
		void Bind(uint32_t slot = 0) const override;
		const std::filesystem::path& GetPath() const override;

		uint32_t GetRenererID() const override;

		

	private:
		std::filesystem::path m_Path;
		TextureSpecification m_Specification;
		Buffer m_Buffer;
	};

	class VulkanTextureCube :public TextureCube
	{

	};
}