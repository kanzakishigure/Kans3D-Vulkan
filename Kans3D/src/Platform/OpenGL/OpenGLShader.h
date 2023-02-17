#pragma once
#include "Kans3D/Renderer/Shader.h"
#include <glm/glm.hpp>

typedef unsigned int GLenum;
namespace Kans
{
	class OpenGLShader : public Shader
	{
	public:
		OpenGLShader(const std::string& name, const std::string& VertexShaderpath, const std::string& FragmentShaderpath);
		OpenGLShader(const std::string& shaderpath);
		
	
		virtual ~OpenGLShader();

		virtual void Bind() const override;
		virtual void UnBind() const override;

		virtual const std::string& GetName() const override { return m_Name; }

		void UploadUniformMat4(const std::string& name, const glm::mat4& matrix)const ;

		void UploadUniform4Float(const std::string& name, const glm::vec4& value)const ;
		void UploadUniform3Float(const std::string& name, const glm::vec3& value)const;
		void UploadUniform2Float(const std::string& name, const glm::vec2& value)const;
		void UploadUniformFloat(const std::string& name, const float value)const;

		void UploadUniform2Int(const std::string& name, const glm::ivec2& value)const;
		void UploadUniformInt(const std::string& name, const int value)const;
		void UploadUniformIntArray(const std::string& name,const int count ,const int* value)const;

		void UploadUniformBool(const std::string& name, const bool value)const;
	public:
		virtual void SetMat4(const std::string& name, const glm::mat4& value) override;
		virtual void SetFloat(const std::string& name, float value) override;
		virtual void SetFloat2(const std::string& name, const glm::vec2& value) override;
		virtual void SetFloat3(const std::string& name, const glm::vec3& value) override;
		virtual void SetFloat4(const std::string& name, const glm::vec4& value) override;
		virtual void SetInt(const std::string& name, int value) override;
		virtual void SetInt2(const std::string& name,const glm::ivec2& value) override;
		virtual void SetIntArray(const std::string& name, const int count, const int* value) override;
		virtual void SetBool(const std::string& name, const bool value) override;
	public:
		virtual const ShaderBuffer& GetShaderBuffer()  const override { return m_Shaderbuffer; }
		virtual const void SetShaderBuffer(ShaderBufferLayout layout)  override;
		
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