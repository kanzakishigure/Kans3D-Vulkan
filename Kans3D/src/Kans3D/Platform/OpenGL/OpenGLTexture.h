#pragma once
#include "Kans3D/Renderer/Resource/Texture.h"
#include <glad/glad.h>


namespace Kans {


	class OpenGLTexture2D :public Texture2D 
	{
	public:
		OpenGLTexture2D(const TextureSpecification& specification, const std::filesystem::path& filepath);
		OpenGLTexture2D(const TextureSpecification& specification, Buffer data = Buffer());
		//重写Texture的虚析构函数，保证从虚函数表能查找到对应析构函数
		virtual ~OpenGLTexture2D() override;

		virtual void Bind(uint32_t slot = 0) const override;

		
		
		virtual uint32_t GetRenererID() const override { return m_RendererID; }
		virtual uint32_t GetHeight() const override { return m_Height; };
		virtual uint32_t GetWidth() const override { return m_Width; };
		virtual const std::filesystem::path& GetPath() const override;
	
	private:
		uint32_t m_Height;
		uint32_t m_Width;
		uint32_t m_RendererID;
		GLenum m_InternalFormat, m_DataFormat;
		std::filesystem::path m_Path;
	};

	class OpenGLTextureCube :public TextureCube
	{
	public:
		OpenGLTextureCube(const TextureSpecification& specification, Buffer data = Buffer());
		virtual ~OpenGLTextureCube() override;

		virtual void Bind(uint32_t slot = 0) const override;
		


		virtual uint32_t GetRenererID() const override { return m_RendererID; }
		virtual uint32_t GetHeight() const override { return m_Height; };
		virtual uint32_t GetWidth() const override { return m_Width; };
		virtual const std::filesystem::path& GetPath() const override;

	

		void GenerateMipmap() const override;

	private:
		uint32_t m_Height;
		uint32_t m_Width;
		uint32_t m_RendererID;
		GLenum m_InternalFormat, m_DataFormat;
		std::filesystem::path m_Path;
	};
}