#include "kspch.h"
#include "Shader.h"

#include "kans3D/Renderer/Renderer.h"
#include "Platform/OpenGL/OpenGLShader.h"

namespace Kans {

	Ref<Shader> Kans::Shader::Create(const std::string& name, const std::string& VertexShader, const std::string& FragmentShader)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPIType::NONE:    HZ_CORE_ASSERT(false, "RendererAPI::NONE is not support"); return nullptr;
		case RendererAPIType::OPENGL:  return CreateRef<OpenGLShader>(name, VertexShader, FragmentShader);
		}
		HZ_CORE_ASSERT(false, "unknow RendererAPI");
		return nullptr;
	}
	Ref<Shader> Shader::Create(const std::string& path)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPIType::NONE:    HZ_CORE_ASSERT(false, "RendererAPI::NONE is not support"); return nullptr;
		case RendererAPIType::OPENGL:  return CreateRef<OpenGLShader>(path);
		}
		HZ_CORE_ASSERT(false, "unknow RendererAPI");
		return nullptr;
	}


	ShaderLibrary::ShaderLibrary()
	{

	}

	ShaderLibrary::~ShaderLibrary()
	{

	}

	void ShaderLibrary::Add(Ref<Shader>& shader)
	{
		auto name = shader->GetName();
		Add(name,shader);
	}

	void ShaderLibrary::Add(const std::string& name, Ref<Shader>& shader)
	{
		HZ_CORE_ASSERT(!Exists(name), "shader already exists!");
		m_Shaders[name] = shader;
	}

	Kans::Ref<Kans::Shader> ShaderLibrary::Load(const std::string& Filepath)
	{
		auto Shader = Shader::Create(Filepath);
		Add(Shader);
		return Shader;
	}

	Kans::Ref<Kans::Shader> ShaderLibrary::Load(const std::string& name, const std::string& Filepath)
	{
		auto Shader = Shader::Create(Filepath);
		Add(name,Shader);
		return Shader;
	}

	Kans::Ref<Kans::Shader> ShaderLibrary::Get(const std::string& name)
	{
		HZ_CORE_ASSERT(ShaderLibrary::Exists(name), "shader not exists!");
		return m_Shaders[name];
	}

	bool ShaderLibrary::Exists(const std::string& name) const
	{
		return m_Shaders.find(name) != m_Shaders.end();
	}

}
