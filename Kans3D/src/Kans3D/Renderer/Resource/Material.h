#pragma once
#include "Shader.h"
#include "Kans3D/Core/Base/Base.h"
#include "Texture.h"
namespace Kans
{
	class Material : public RefCounter
	{
	public:
		 Material(){}
		 virtual ~Material() {}
		 virtual const Ref<Shader> GetShader()const = 0;
	public:
		static Ref<Material> Create(const Ref<Shader>& shader,const std::string& name = "");
		
		virtual void Set(const std::string& name, const glm::mat4& value) = 0;
		virtual void Set(const std::string& name, float value) = 0;
		virtual void Set(const std::string& name, const glm::vec2& value) = 0;
		virtual void Set(const std::string& name, const glm::vec3& value) = 0;
		virtual void Set(const std::string& name, const glm::vec4& value) = 0;
		virtual void Set(const std::string& name, int value) = 0;
		virtual void Set(const std::string& name, bool value) = 0;
		virtual void Set(const std::string& name, const glm::ivec2& value) =0;

		virtual void SetTexture(const std::string& name, Ref<Texture> value) = 0;
		virtual void SetTextures(const std::map<std::string, Ref<Texture>>& textures) = 0;
		virtual const std::map<std::string, Ref<Texture>>& GetTextures() const = 0;
		virtual std::map<std::string, Ref<Texture>>& GetTextures() = 0;

		virtual void SetIntArray(const std::string& name, const int count, const int* value) = 0;
		virtual const std::string& GetName()const =0;
	public:
		virtual  glm::mat4& GetMat4(const std::string& name)  = 0;
		virtual float& GetFloat(const std::string& name)      = 0;
		virtual glm::vec2& GetVec2(const std::string& name)   = 0;
		virtual glm::vec3& GetVec3(const std::string& name)   = 0;
		virtual glm::vec4& GetVec4(const std::string& name)   = 0;
		virtual glm::ivec2& GetIVec2(const std::string& name) = 0;
		virtual int& GetInt(const std::string& name) = 0;
		virtual bool& GetBool(const std::string& name) = 0;
		
	public:
		virtual void SetShader(Ref<Shader>shader) =0;
		virtual const ShaderBuffer& GetShaderBuffer() const = 0;

		virtual void Invalidate() = 0;
		
		
		
	};
}