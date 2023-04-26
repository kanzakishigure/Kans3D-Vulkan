#pragma once

#include "Kans3D/Asset/Asset.h"
namespace Kans {

	class Texture : public Asset
	{
	public:
		virtual ~Texture() = default;

		virtual uint32_t GetHeight() const = 0;
		virtual uint32_t GetWidth() const = 0;
		virtual uint32_t GetRenererID() const = 0;

		virtual void SetData(void* data, uint32_t size) = 0;
		virtual void Bind(uint32_t slot = 0)const = 0;
		virtual const std::string GetPath() const = 0;
		
	public:
		virtual bool operator==(const Texture& other) const = 0;

		
	};
	class Texture2D :public Texture
	{

	public:
		static	Ref<Texture2D> Create(const std::string& path);
		static	Ref<Texture2D> Create(uint32_t width,uint32_t height);

	};

	class TextureCube :public Texture
	{

	public:
		static	Ref<TextureCube> Create(const std::string& path);
		static	Ref<TextureCube> Create(uint32_t width, uint32_t height);

	};

}