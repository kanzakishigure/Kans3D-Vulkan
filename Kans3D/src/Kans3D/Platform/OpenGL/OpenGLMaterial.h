#pragma once
#include <glm/glm.hpp>
#include "kans3D/Renderer/Resource/Material.h"
#include "Kans3D/Renderer/Resource/Texture.h"
#include "Kans3D/Core/ByteBuffer.h"
namespace Kans {
	//参照cherno的vulkan版材质类进行进行改写

	
	class OpenGLMaterial :public Material
	{
	public:
		OpenGLMaterial(const Ref<Shader>& shader, const std::string& name = " ");
		 ~OpenGLMaterial();

		void Init();
		void AllocBuffer();
		virtual const std::string& GetName()const override;

		virtual const Ref<Shader> GetShader()const override { return m_Shader;}
		virtual void SetShader(Ref<Shader>shader) override;

		virtual const ShaderBuffer& GetShaderBuffer()const override { return m_Shader->GetShaderBuffer(); }
		
		//set the uniform data
		//use the uniform buffer we can set the material stats once;
		virtual void Invalidate()  override;
	public:
		virtual void Set(const std::string& name, const glm::mat4& value) override;
		virtual void Set(const std::string& name, float value) override;
		virtual void Set(const std::string& name, const glm::vec2& value) override;
		virtual void Set(const std::string& name, const glm::vec3& value) override;
		virtual void Set(const std::string& name, const glm::vec4& value) override;
		virtual void Set(const std::string& name, bool value)  override;

		virtual void Set(const std::string& name, int value) override;
		virtual void Set(const std::string& name, const glm::ivec2& value) override;
		virtual void SetIntArray(const std::string& name, const int count, const int* value) override;

		virtual void SetTexture(const std::string& name, Ref<Texture> value) override;
		virtual void SetTextures(const std::map<std::string, Ref<Texture>>& textures) override { m_Textures = textures; }
		virtual const std::map<std::string, Ref<Texture>>& GetTextures() override { return m_Textures; }

		virtual glm::mat4& GetMat4(const std::string& name) override;
		virtual float& GetFloat(const std::string& name)     override;
		virtual glm::vec2& GetVec2(const std::string& name)  override;
		virtual glm::vec3& GetVec3(const std::string& name)  override;
		virtual glm::vec4& GetVec4(const std::string& name)  override;
		virtual int& GetInt(const std::string& name)         override;
		virtual glm::ivec2& GetIVec2(const std::string& name) override;
		virtual bool& GetBool(const std::string& name) override;

		
	public:
		
		template <typename T>
		void Set(const std::string& name, const T& value)
		{
			auto uniform = FindUniform(name);
			if (!uniform)
			{
				CORE_ASSERT(uniform, "Can not find uniform");
				return;
			}
			m_UniformBuffer.Write((byte*)&value,  uniform->GetOffset(), uniform->GetSize());
		}
		template <typename T>
		T& Get(const std::string& name)
		{
			auto uniform = FindUniform(name);
			if (!uniform)
			{
				CORE_ASSERT(uniform, "Can not find uniform");
			}
			return m_UniformBuffer.Read<T>(uniform->GetOffset());
		}
	private: 
		ShaderUniform* FindUniform(const std::string& name);
	private:
		Ref<Shader> m_Shader;
		std::string m_Name;

		Buffer m_UniformBuffer;
		//texture 
		std::map<std::string, Ref<Texture>> m_Textures;
		//Setting
		bool m_UseDefaultShader = true;
	};
}