#include "hzpch.h"

#include "OpenGLShader.h"

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Kans {

	static GLenum ShaderTypeFromString(const std::string& type)
	{
		if (type == "vertex") 
			return GL_VERTEX_SHADER;
		if (type == "fragment") 
			return GL_FRAGMENT_SHADER;
		if (type == "geometry")
			return GL_GEOMETRY_SHADER;
		HZ_CORE_ASSERT(false, "Unknow shadertype");
		return 0;
	}
	OpenGLShader::OpenGLShader(const std::string& shaderpath)
	{
		HZ_PROFILE_FUCTION();

		std::string shadersrc = ReadFile(shaderpath);
		auto source = PreProcess(shadersrc);
		Compile(source);
		//从文件名获取shader的命名
		auto lastSlash = shaderpath.find_last_of("/\\");
		lastSlash = lastSlash == std::string::npos ? 0 : lastSlash + 1;
		auto lastDot = shaderpath.rfind('.');
		auto count = lastDot == std::string::npos ? shaderpath.size() - lastSlash : lastDot - lastSlash;
		m_Name = shaderpath.substr(lastSlash,count);
	}
	OpenGLShader::OpenGLShader(const std::string& name, const std::string& VertexShaderpath, const std::string& FragmentShaderpath)
	{
		HZ_PROFILE_FUCTION();

		std::unordered_map<GLenum, std::string> shadersource;
		std::string t= ReadFile(VertexShaderpath);
		shadersource[GL_VERTEX_SHADER] = ReadFile(VertexShaderpath);
		shadersource[GL_FRAGMENT_SHADER] =ReadFile(FragmentShaderpath);
		Compile(shadersource);
		m_Name = name;
	}


	OpenGLShader::~OpenGLShader()
	{
		HZ_PROFILE_FUCTION();

		glDeleteProgram(m_RendererID);
	}

	std::string OpenGLShader::ReadFile(const std::string& filepath)
	{
		HZ_PROFILE_FUCTION();

		std::string result;
		std::ifstream in(filepath, std::ios::in|std::ios::binary);
		if (in)
		{
			//为当前文件流设置下一个读取的流位置
			/*
			ios_base::beg	beginning of the stream
			ios_base::cur	current position in the stream
			ios_base::end	end of the stream
			*/
					//偏移量，类型
			in.seekg(0, std::ios::end);
			//将tellg移动到文件尾
			result.resize(in.tellg());
			//tellg() 
			//Returns the position of the current character in the input stream.
			in.seekg(0, std::ios::beg);

			in.read(&result[0], result.size());
			in.close();
		}
		else
		{
			HZ_CORE_ERROR("could not open the file '{0}'", filepath);
		}
		return result;
	}

	std::unordered_map<GLenum, std::string> OpenGLShader::PreProcess(const std::string& source)
	{
		HZ_PROFILE_FUCTION();

		const char* typeToken = "#type";
		size_t typeTokenlength = strlen(typeToken);
		size_t pos = source.find_first_of(typeToken);
		std::unordered_map<GLenum, std::string> shadersource;
		while (pos!=std::string::npos)
		{
			size_t eol = source.find_first_of("\n\r",pos);
			HZ_CORE_ASSERT(eol != std::string::npos, "Syntax error !");
			size_t begin = pos + typeTokenlength + 1;
			std::string type = source.substr(begin, eol-begin);
			HZ_CORE_ASSERT(ShaderTypeFromString(type), "invalid shader type !");
			size_t nextlinepos = source.find_first_not_of("\r\n", eol);
			pos = source.find(typeToken, nextlinepos);
			shadersource[ShaderTypeFromString(type)] = source.substr(
				nextlinepos, pos - (nextlinepos == std::string::npos ? source.size() - 1 : nextlinepos)
			);

		}
		return shadersource;
	}

	void OpenGLShader::Compile(const std::unordered_map<GLenum, std::string>& shadersource)
	{
		HZ_PROFILE_FUCTION();

		//shaderComplie
		uint32_t id = glCreateProgram();
		HZ_CORE_ASSERT(shadersource.size() <= 3, "shadersource is not support !");
		std::array<GLenum,3> glShaderIDs;
		uint32_t index=0;
		GLenum test1 = GL_FRAGMENT_SHADER;
		GLenum test2 = GL_VERTEX_SHADER;
		int test3 = GL_GEOMETRY_SHADER;
		for each (auto & kv in shadersource)
		{
			GLenum type = kv.first;
			std::string source = kv.second;
			uint32_t shader = glCreateShader(type);
			GLint isCompiled = 0;
			const GLchar* sourceStr = source.c_str();
			glShaderSource(shader, 1, &sourceStr, NULL);
			glCompileShader(shader);
			//查看是否编译成功
			glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
			if (isCompiled == GL_FALSE)
			{
				GLint maxLength = 0;
				glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);
				std::vector<GLchar> infolog(maxLength);
				glGetShaderInfoLog(shader, maxLength, &maxLength, &infolog[0]);

				HZ_CORE_ERROR("{0}", infolog.data());
				
				HZ_CORE_ASSERT(false, "shader compilation failure!{0}");
				//删除shader文件
				glDeleteShader(shader);
				break;
			}
			glAttachShader(id, shader);
			glShaderIDs[index++]=shader;
		}


		//shaderProgram
		GLint isLinked = 0;

		glLinkProgram(id);
		glGetProgramiv(id, GL_LINK_STATUS, (int*)&isLinked);

		if (isLinked == GL_FALSE)
		{
			GLint maxLength = 0;
			glGetProgramiv(id, GL_INFO_LOG_LENGTH, &maxLength);

			std::vector<GLchar> infolog(maxLength);
			glGetProgramInfoLog(id, maxLength, &maxLength, &infolog[0]);

			HZ_CORE_ERROR("{0}", infolog.data());
			HZ_CORE_ASSERT(false, "shaderpragram link failure!");
			//防止内存泄漏，删除程序
			glDeleteProgram(id);
			// 同理.
			for each (auto shaderid in glShaderIDs)
			{
				glDeleteShader(shaderid);
			}
			return;
		}
		//成功链接之后不用继续链接
		for each (auto shaderid in glShaderIDs)
		{
			glDetachShader(id,shaderid);
			glDeleteShader(shaderid);
		}
		m_RendererID = id;
	}





	void OpenGLShader::Bind() const
	{
		HZ_PROFILE_FUCTION();

		glUseProgram(m_RendererID);
	}

	void OpenGLShader::UnBind() const
	{
		HZ_PROFILE_FUCTION();

		glUseProgram(0);
	}

	void OpenGLShader::UploadUniformMat4(const std::string& name, const glm::mat4& matrix) const
	{
		uint32_t location = glGetUniformLocation(m_RendererID, name.c_str());
		glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(matrix));
	}

	void OpenGLShader::UploadUniform4Float(const std::string& name, const glm::vec4& value) const
	{
		uint32_t location = glGetUniformLocation(m_RendererID, name.c_str());
		glUniform4fv(location,1, glm::value_ptr(value));
		//glUniform4f(location, value.x, value.y, value.z,value.w);

	}

	void OpenGLShader::UploadUniform3Float(const std::string& name, const glm::vec3& value) const
	{
		uint32_t location = glGetUniformLocation(m_RendererID, name.c_str());
		glUniform3f(location, value.x, value.y, value.z);
	}

	void OpenGLShader::UploadUniform2Float(const std::string& name, const glm::vec2& value) const
	{
		uint32_t location = glGetUniformLocation(m_RendererID, name.c_str());
		glUniform2f(location, value.x, value.y);
	}

	void OpenGLShader::UploadUniformFloat(const std::string& name, const float value) const
	{
		uint32_t location = glGetUniformLocation(m_RendererID, name.c_str());
		glUniform1f(location, value);
	}

	void OpenGLShader::UploadUniform2Int(const std::string& name, const glm::ivec2& value) const
	{
		uint32_t location = glGetUniformLocation(m_RendererID, name.c_str());
		glUniform2i(location, value.x, value.y);
	}

	void OpenGLShader::UploadUniformInt(const std::string& name, const int value) const
	{
		uint32_t location = glGetUniformLocation(m_RendererID, name.c_str());
		glUniform1i(location, value);
	}

	void OpenGLShader::UploadUniformIntArray(const std::string& name,const int count ,const int* value) const
	{
		uint32_t location = glGetUniformLocation(m_RendererID, name.c_str());
		glUniform1iv(location,count,value);
	}

	void OpenGLShader::UploadUniformBool(const std::string& name, const bool value) const
	{
		uint32_t location = glGetUniformLocation(m_RendererID, name.c_str());
		glUniform1i(location, value);
	}

	void OpenGLShader::SetMat4(const std::string& name, const glm::mat4& value)
	{
		HZ_PROFILE_FUCTION();
		UploadUniformMat4(name, value);
	}

	void OpenGLShader::SetFloat(const std::string& name, float value)
	{
		HZ_PROFILE_FUCTION();
		UploadUniformFloat(name, value);
	}

	void OpenGLShader::SetFloat2(const std::string& name, const glm::vec2& value)
	{
		HZ_PROFILE_FUCTION();
		UploadUniform2Float(name, value);
	}

	void OpenGLShader::SetFloat3(const std::string& name, const glm::vec3& value)
	{
		HZ_PROFILE_FUCTION();
		UploadUniform3Float(name, value);
	}

	void OpenGLShader::SetFloat4(const std::string& name, const glm::vec4& value)
	{
		//HZ_PROFILE_FUCTION();
		UploadUniform4Float(name, value);
	}

	void OpenGLShader::SetInt(const std::string& name, int value)
	{
		HZ_PROFILE_FUCTION();
		UploadUniformInt(name, value);
	}

	void OpenGLShader::SetInt2(const std::string& name, const glm::ivec2& value)
	{
		HZ_PROFILE_FUCTION();
		UploadUniform2Int(name, value);
	}

	void OpenGLShader::SetIntArray(const std::string& name, const int count, const int* value)
	{
		HZ_PROFILE_FUCTION();
		UploadUniformIntArray(name,count,value);
	}

	void OpenGLShader::SetBool(const std::string& name, const bool value)
	{
		HZ_PROFILE_FUCTION();
		UploadUniformBool(name, value);
	}

	const void OpenGLShader::SetShaderBuffer(ShaderBufferLayout layout)
	{
		uint32_t size = 0;
		for (auto& element : layout.GetElements())
		{
			m_Shaderbuffer.ShaderUniforms[element.Name] = ShaderUniform{element.Name,element.Size,element.Offset,element.Type};
			size += element.Size;
		}
		m_Shaderbuffer.Size = size;

	}

}