#pragma once
#include "Kans3D/Asset/Asset.h"
#include "Kans3D/Renderer/RHI/RHITypeProxy.h"
#include "Kans3D/Core/ByteBuffer.h"
#include <filesystem>

namespace Kans {

	struct TextureSpecification
	{
		uint32_t Width{ 0 };
		uint32_t Height{ 0 };
		uint32_t Depth{ 0 };
		uint32_t Mip_levels{ 0 };
		uint32_t Array_layers{ 0 };
		RHIFormat Format = RHI_FORMAT_MAX_ENUM;
	}; 

	class Texture : public Asset
	{
	public:
		virtual ~Texture() = default;

		virtual uint32_t GetHeight() const = 0;
		virtual uint32_t GetWidth() const = 0;
		virtual uint32_t GetRenererID() const = 0;

		virtual void Bind(uint32_t slot = 0)const = 0;
		virtual const std::filesystem::path& GetPath() const = 0;
		

		
	};
	class Texture2D :public Texture
	{

	public:
		static	Ref<Texture2D> Create(const TextureSpecification& specification,Buffer buffer = Buffer());
		static	Ref<Texture2D> Create(const TextureSpecification& specification, const std::filesystem::path& filepath);

	};

	class TextureCube :public Texture
	{

	public:
		static	Ref<TextureCube> Create(const std::string& path);
		static	Ref<TextureCube> Create(uint32_t width, uint32_t height);

	};

}