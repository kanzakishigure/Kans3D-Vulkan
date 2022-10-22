#pragma once
#include "Kans3D/Renderer/Texture.h"
#include <glad/glad.h>
namespace Kans {


	class OpenGLTexture2D :public Texture2D 
	{
	public:
		OpenGLTexture2D(const std::string& path);
		OpenGLTexture2D(uint32_t width,uint32_t height);
		//重写Texture的虚析构函数，保证从虚函数表能查找到对应析构函数
		virtual ~OpenGLTexture2D() override;

		virtual void Bind(uint32_t slot = 0) const override;

		virtual void SetData(void* data, uint32_t size) override;
		
		virtual uint32_t GetRenererID() const override { return m_RendererID; }
		virtual uint32_t GetHeight() const override { return m_Height; };
		virtual uint32_t GetWidth() const override { return m_Width; };
		virtual const std::string GetPath() const override { return path; }
	public:
		virtual bool operator==(const Texture& other) const override
		{
			//在进行了强制类型转换后，通过引用访问到了私有变量
			return m_RendererID == ((OpenGLTexture2D&)other).m_RendererID;
		}
	private:
		uint32_t m_Height;
		uint32_t m_Width;
		uint32_t m_RendererID;
		GLenum m_InternalFormat, m_DataFormat;
		std::string path;
	};

	class OpenGLTextureCube :public TextureCube
	{
	public:
		OpenGLTextureCube(const std::string& path);
		OpenGLTextureCube(uint32_t width, uint32_t height);
		virtual ~OpenGLTextureCube() override;

		virtual void Bind(uint32_t slot = 0) const override;
		virtual void SetData(void* data, uint32_t size) override;


		virtual uint32_t GetRenererID() const override { return m_RendererID; }
		virtual uint32_t GetHeight() const override { return m_Height; };
		virtual uint32_t GetWidth() const override { return m_Width; };
		virtual const std::string GetPath() const override { return path; }

	public:
		virtual bool operator==(const Texture& other) const override
		{
			//在进行了强制类型转换后，通过引用访问到了私有变量
			return m_RendererID == ((OpenGLTextureCube&)other).m_RendererID;
		}
	private:
		uint32_t m_Height;
		uint32_t m_Width;
		uint32_t m_RendererID;
		GLenum m_InternalFormat, m_DataFormat;
		std::string path;
	};
}