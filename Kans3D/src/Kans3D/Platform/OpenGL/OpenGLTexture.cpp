#include "kspch.h"
#include "OpenGLTexture.h"
#include <stb_image.h>


namespace Kans {


	OpenGLTexture2D::OpenGLTexture2D(const TextureSpecification& specification, const std::filesystem::path& filepath)
		:m_Path(filepath)
	{
		HZ_PROFILE_FUCTION();

		int width, height, channel;
		std::string path = m_Path.generic_string();
		//保证与opengl中的uv坐标系吻合，翻转y轴
		stbi_set_flip_vertically_on_load(1);
		stbi_uc* data;
		{
			HZ_PROFILE_SCOPE("stbi_load() - OpenGLTexture2D::OpenGLTexture2D(const std::string& )")
				data = stbi_load(path.c_str(), &width, &height, &channel, 0);
			//文件路径，宽，高，通道，所需通道
			CORE_ASSERT(data, "texture load failure ");

		}


		m_Height = height;
		m_Width = width;

		switch (channel)
		{
		case 3: m_InternalFormat = GL_RGB8;  m_DataFormat = GL_RGB; break;
		case 4: m_InternalFormat = GL_RGBA8; m_DataFormat = GL_RGBA; break;
		}
		CORE_ASSERT(m_InternalFormat & m_DataFormat, "Texture format error !");

		glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);
		glTextureStorage2D(m_RendererID, 1, m_InternalFormat, m_Width, m_Height);

		glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, GL_REPEAT);
		//ID,贴图位置，x偏移量，y偏移量，宽，高，通道，数据类型，数据
		glTextureSubImage2D(m_RendererID, 0, 0, 0, m_Width, m_Height, m_DataFormat, GL_UNSIGNED_BYTE, data);
		stbi_image_free(data);
	}

	OpenGLTexture2D::OpenGLTexture2D(const TextureSpecification& specification, Buffer data /*= Buffer()*/)
		:m_Width(specification.Width), m_Height(specification.Height)
	{
		HZ_PROFILE_FUCTION();

		switch (specification.Format)
		{
		case RHIFormat::RHI_FORMAT_R8G8B8A8_SRGB:
			{
				m_InternalFormat = GL_RGBA8;
				m_DataFormat = GL_RGBA;
			}
			break;
		case RHIFormat::RHI_FORMAT_R8G8B8_SRGB:
			{
				m_InternalFormat = GL_RGB8;
				m_DataFormat = GL_RGB;
			}
			break;
		default:
			break;
		}
		CORE_ASSERT(m_InternalFormat & m_DataFormat, "Texture format error !");

		glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);
		glTextureStorage2D(m_RendererID, 1, m_InternalFormat, m_Width, m_Height);

		glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, GL_REPEAT);

		

		uint32_t bpc = m_DataFormat == GL_RGBA ? 4 : 3;
		CORE_ASSERT(data.Size == m_Width * m_Height * bpc, "Data must enetire texture !")
			glTextureSubImage2D(m_RendererID, 0, 0, 0, m_Width, m_Height, m_DataFormat, GL_UNSIGNED_BYTE, data.Data);
	}

	OpenGLTexture2D::~OpenGLTexture2D()
	{
		HZ_PROFILE_FUCTION();

		glDeleteTextures(1,&m_RendererID);
	}

	void OpenGLTexture2D::Bind(uint32_t slot) const
	{
		HZ_PROFILE_FUCTION();
		glBindTextureUnit(slot, m_RendererID);
	}



	
	const std::filesystem::path& OpenGLTexture2D::GetPath() const
	{
		return m_Path;
	}

	OpenGLTextureCube::OpenGLTextureCube(const std::string& path)
	{

	}

	OpenGLTextureCube::OpenGLTextureCube(uint32_t width, uint32_t height)
	{

	}

	OpenGLTextureCube::~OpenGLTextureCube()
	{

	}

	void OpenGLTextureCube::Bind(uint32_t slot /*= 0*/) const
	{

	}

	const std::filesystem::path& OpenGLTextureCube::GetPath() const
	{
		return m_Path;
	}

	

}