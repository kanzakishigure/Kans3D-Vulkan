#pragma once
#include "Kans3D/Renderer/Resource/Shader.h"
#include <glm/glm.hpp>

typedef unsigned int GLenum;
namespace Kans
{
	class OpenGLShader : public Shader
	{
	public:
		OpenGLShader() = default;
		OpenGLShader(const std::string& shaderpath);
		OpenGLShader(const std::string& name, const std::string& VertexShaderpath, const std::string& FragmentShaderpath);
		virtual ~OpenGLShader();
		
		virtual void Reload(bool forceCompile = false) override;
		virtual void RT_Reload(bool forceCompile) override;

		virtual const std::string& GetName() const override { return m_Name; }

		//will be discard
		virtual const ShaderBuffer& GetShaderBuffer()  const override { return m_Shaderbuffer;}
		virtual void SetShaderBuffer(ShaderBufferLayout layout)  override;

	public:
		void Bind() const;
		void UnBind() const;
		
		void UploadUniformMat4(const std::string& name, const glm::mat4& matrix)const;
		void UploadUniform4Float(const std::string& name, const glm::vec4& value)const;
		void UploadUniform3Float(const std::string& name, const glm::vec3& value)const;
		void UploadUniform2Float(const std::string& name, const glm::vec2& value)const;
		void UploadUniformFloat(const std::string& name, const float value)const;
		void UploadUniform2Int(const std::string& name, const glm::ivec2& value)const;
		void UploadUniformInt(const std::string& name, const int value)const;
		void UploadUniformIntArray(const std::string& name, const int count, const int* value)const;
		void UploadUniformBool(const std::string& name, const bool value)const;
	private:
		
		std::string ReadFile(const std::string& filepath);
		std::unordered_map<GLenum,std::string> PreProcess(const std::string& source);
		void Compile(const std::unordered_map<GLenum, std::string>& shadersource);
	private:
		ShaderBuffer m_Shaderbuffer;
		uint32_t m_RendererID;
		std::string m_Name;
	};
}