#include "kspch.h"
#include "OpenGLTexture.h"
#include <stb_image.h>

#include "Kans3D/Renderer/RHI/OpenGL/OpenGLRenderCommand.h"
namespace Kans {

	namespace Utils
	{
		GLenum RHIWrapTypeToGLType(RHISamplerAddressMode Wrap)
		{
			switch (Wrap)
			{
			case RHISamplerAddressMode::RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE: return  GL_CLAMP_TO_EDGE;
			case RHISamplerAddressMode::RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:  return GL_CLAMP_TO_BORDER; 
			case RHISamplerAddressMode::RHI_SAMPLER_ADDRESS_MODE_REPEAT: return GL_REPEAT; 
			default:
				CORE_ASSERT(false, "Unknow Wrap Type");
			}
		}

		GLenum RHIFilterTypeToGLType(RHIFilter filter)
		{
			switch (filter)
			{
			case RHIFilter::RHI_FILTER_LINEAR :return  GL_LINEAR;
			case RHIFilter::RHI_FILTER_NEAREST:  return GL_NEAREST;
			case RHIFilter::RHI_FILTER_CUBIC_EXT :  return GL_LINEAR_MIPMAP_LINEAR;
			default:
				CORE_ASSERT(false, "Unknow Filter Type");
			}

		}
	}
	OpenGLTexture2D::OpenGLTexture2D(const TextureSpecification& specification, const std::filesystem::path& filepath)
		:m_Path(filepath)
	{
		PROFILE_FUCTION();

		int width, height, channel;
		std::string path = m_Path.generic_string();
		//保证与opengl中的uv坐标系吻合，翻转y轴
		stbi_set_flip_vertically_on_load(1);

		Buffer imageBuffer;
		if (stbi_is_hdr(path.c_str()))
		{
			imageBuffer.Data = stbi_loadf(path.c_str(), &width, &height, &channel, 0);
			imageBuffer.Size = width * height * 4 * sizeof(float);

			switch (channel)
			{
			case 3: m_InternalFormat = GL_RGB16F;  m_DataFormat = GL_RGB; break;
			case 4: m_InternalFormat = GL_RGBA16F; m_DataFormat = GL_RGBA; break;
			}
		}
		else
		{
			
			imageBuffer.Data = stbi_load(path.c_str(), &width, &height, &channel, 0);
			imageBuffer.Size = width * height * 4;
			switch (channel)
			{
			case 3: m_InternalFormat = GL_RGB8;  m_DataFormat = GL_RGB; break;
			case 4: m_InternalFormat = GL_RGBA8; m_DataFormat = GL_RGBA; break;
			}
		}
		


		m_Height = height;
		m_Width = width;

		
		CORE_ASSERT(m_InternalFormat & m_DataFormat, "Texture format error !");

		glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);
		glTextureStorage2D(m_RendererID, 1, m_InternalFormat, m_Width, m_Height);

		glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, (GLuint)Utils::RHIFilterTypeToGLType(specification.Minf));
		glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, (GLuint)Utils::RHIFilterTypeToGLType(specification.Minf));

		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, Utils::RHIWrapTypeToGLType(specification.Wrap));
		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, Utils::RHIWrapTypeToGLType(specification.Wrap));
		//ID,贴图位置，x偏移量，y偏移量，宽，高，通道，数据类型，数据
		if (stbi_is_hdr(path.c_str()))
		{
			glTextureSubImage2D(m_RendererID, 0, 0, 0, m_Width, m_Height, m_DataFormat, GL_FLOAT, imageBuffer.Data);
			
		}
		else
		{
			glTextureSubImage2D(m_RendererID, 0, 0, 0, m_Width, m_Height, m_DataFormat, GL_UNSIGNED_BYTE, imageBuffer.Data);
			
		}
		stbi_image_free(imageBuffer.Data);
		
	}

	OpenGLTexture2D::OpenGLTexture2D(const TextureSpecification& specification, Buffer data /*= Buffer()*/)
		:m_Width(specification.Width), m_Height(specification.Height)
	{
		PROFILE_FUCTION();

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
		case RHIFormat::RHI_FORMAT_R16G16B16A16_SFLOAT:
		{
			m_InternalFormat = GL_RGBA16F;
			m_DataFormat = GL_RGBA;
		}break;
		case RHIFormat::RHI_FORMAT_R16G16B16_SFLOAT:
		{
			m_InternalFormat = GL_RGB16F;
			m_DataFormat = GL_RGB;
		}break;
		case RHIFormat::RHI_FORMAT_R16G16_SFLOAT:
		{
			m_InternalFormat = GL_RG16F;
			m_DataFormat = GL_RG;
		}
		break;
		default:
			break;
		}
		CORE_ASSERT(m_InternalFormat & m_DataFormat, "Texture format error !");

		glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);
		glTextureStorage2D(m_RendererID, 1, m_InternalFormat, m_Width, m_Height);

		glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, (GLuint)Utils::RHIFilterTypeToGLType(specification.Minf));
		glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, (GLuint)Utils::RHIFilterTypeToGLType(specification.Maxf));

		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, Utils::RHIWrapTypeToGLType(specification.Wrap));
		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, Utils::RHIWrapTypeToGLType(specification.Wrap));

		
		GLenum ByteType = GL_UNSIGNED_BYTE;
		switch (m_InternalFormat)
		{
		case GL_RGBA16F:;
		case GL_RGB16F:;
		case GL_RG16F:;
			ByteType = GL_FLOAT;
			break;
		default:
			ByteType = GL_UNSIGNED_BYTE;
			break;
		}
		uint32_t bpc = 0;
		switch (m_DataFormat)
		{
		case GL_RGBA: bpc = 4; break;
		case GL_RGB: bpc = 3; break;
		case GL_RG: bpc = 2; break;
		}

		if (data.Data != nullptr)
		{
			CORE_ASSERT(data.Size == m_Width * m_Height * bpc, "Data must enetire texture !");
			glTextureSubImage2D(m_RendererID, 0, 0, 0, m_Width, m_Height, m_DataFormat, ByteType, data.Data);
		}
		else
		{
			CORE_WARN("Texture ID:[{}] create with nullptr ",m_RendererID);
		}
		
	}

	OpenGLTexture2D::~OpenGLTexture2D()
	{
		
		glDeleteTextures(1,&m_RendererID);
	}

	void OpenGLTexture2D::Bind(uint32_t slot) const
	{
		
		glBindTextureUnit(slot, m_RendererID);
	}



	
	const std::filesystem::path& OpenGLTexture2D::GetPath() const
	{
		return m_Path;
	}

	

	OpenGLTextureCube::OpenGLTextureCube(const TextureSpecification& specification, Buffer data /*= Buffer()*/)
	{
		m_Width = specification.Width;
		m_Height = specification.Height;
		switch (specification.Format)
		{
		case RHIFormat::RHI_FORMAT_R16G16B16A16_SFLOAT:
		{
			m_InternalFormat = GL_RGBA16F;
			m_DataFormat = GL_RGBA;
		}break;
		case RHIFormat::RHI_FORMAT_R8G8B8A8_SNORM:
		{
			m_InternalFormat = GL_RGBA8;
			m_DataFormat = GL_RGBA;
		}break;
		case RHIFormat::RHI_FORMAT_R16G16B16_SFLOAT:
		{
			m_InternalFormat = GL_RGB16F;
			m_DataFormat = GL_RGB;
		}break;
		case RHIFormat::RHI_FORMAT_R8G8B8_SNORM:
		{
			m_InternalFormat = GL_RGB8;
			m_DataFormat = GL_RGB;
		}break;
		default:
		{
			CORE_ERROR("unknow Image format in TextureCube Create");
		}
		break;
		}

		glGenTextures(1, &m_RendererID);
		glBindTexture(GL_TEXTURE_CUBE_MAP, m_RendererID);

		GLenum ByteType = GL_UNSIGNED_BYTE;
		switch (m_InternalFormat)
		{
		case GL_RGBA16F:;
		case GL_RGB16F:;
		case GL_RG16F:;
			ByteType = GL_FLOAT;
			break;
		default:
			ByteType = GL_UNSIGNED_BYTE;
			break;
		}
		for (unsigned int i = 0; i < 6; ++i)
		{
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, m_InternalFormat, m_Width, m_Height, 0, m_DataFormat, ByteType, nullptr);
		}



		//贴图边缘处理设置

		
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, Utils::RHIWrapTypeToGLType(specification.Wrap));
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, Utils::RHIWrapTypeToGLType(specification.Wrap));
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, Utils::RHIWrapTypeToGLType(specification.Wrap));
		
		
		
		//贴图插值设置
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, (GLuint)Utils::RHIFilterTypeToGLType(specification.Minf));
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, (GLuint)Utils::RHIFilterTypeToGLType(specification.Maxf));
		
		if (specification.GenerateMips)
		{
			glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
		}
	}

	OpenGLTextureCube::~OpenGLTextureCube()
	{
		glDeleteTextures(1, &m_RendererID);
	}

	void OpenGLTextureCube::Bind(uint32_t slot /*= 0*/) const
	{
		
		glBindTextureUnit(slot, m_RendererID);
	}

	const std::filesystem::path& OpenGLTextureCube::GetPath() const
	{
		return m_Path;
	}

	

	void OpenGLTextureCube::GenerateMipmap() const
	{
		glBindTexture(GL_TEXTURE_CUBE_MAP, m_RendererID);
		glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
	}

}