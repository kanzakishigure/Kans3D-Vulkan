#pragma once

namespace Kans
{

	enum class ImageFormat
	{
		RG16F,
		RGB16F,
		RGBA16F,

		RG8,
		RGB8,
		RGBA8,
	};

	enum class ImageUsage
	{
		None = 0,
		Texture,
		Storage,
		Attachment
	};

	enum class TextureWrap
	{
		None = 0,
		Clamp,
		Repeat
	};

	enum class TextureFilter
	{
		None = 0,
		Linear,
		Nearest
	};
	enum class TextureType
	{
		None = 0,
		Texture2D,
		TextureCube
	};

	struct ImageSpecification
	{
		std::string DebugName;
		uint32_t  Width;
		uint32_t  Height;

		ImageFormat Format = ImageFormat::RGBA8;
		ImageUsage Usage = ImageUsage::Texture;
		uint32_t Mips = 1;

	};


	//Image is the set of all image resource(texture is a special image)
	class Image
	{
	public:
		virtual ~Image() = default;

		virtual void Resize(uint32_t width,uint32_t height) = 0;
		virtual void Release() = 0;
		virtual void Invalidate() = 0;

		virtual uint32_t GetWidth() const = 0;
		virtual uint32_t GetHeight() const = 0;
		virtual glm::uvec2 GetSize() const = 0;
		
		virtual const ImageSpecification& GetSpecification() const = 0;
		virtual ImageSpecification& GetSpecification()  = 0;

	};

	class Image2D : public Image
	{
	public:
		static Ref<Image2D> Create(ImageSpecification spec);
	};
};