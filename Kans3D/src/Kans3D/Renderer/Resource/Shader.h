#pragma once
#include <glm/glm.hpp>
#include "Kans3D/Core/Base/Base.h"
#include "Kans3D/Renderer/RHI/Buffer.h"
namespace Kans
{

	namespace ShaderUtils {
		enum class ShaderLang
		{
			NONE, GLSL, HLSL,
		};
	}

	class ShaderUniform
	{
	public:
		ShaderUniform() = default;
		ShaderUniform(const std::string& name, uint32_t size, uint32_t offset, ShaderDataType type)
			:Name(name), Size(size),Offset(offset),Type(type) {}
		
		std::string& GetName() { return Name; }
		const std::string& GetName() const{ return Name; }

		uint32_t GetSize() { return Size; }
		const  uint32_t GetSize() const { return Size; }

		uint32_t GetOffset() { return Offset; }
		const uint32_t GetOffset() const { return Offset; }

		ShaderDataType GetType() { return Type; }
		const ShaderDataType GetType() const { return Type; }

	private:
		std::string Name;
		uint32_t Size = 0;
		uint32_t Offset = 0;
		ShaderDataType Type = ShaderDataType::NONE;
	};
	struct ShaderBuffer
	{
		std::string Name;
		uint32_t Size = 0;
		std::unordered_map<std::string, ShaderUniform> ShaderUniforms;
	};
	class  ShaderBufferLayout
	{
	public:
		ShaderBufferLayout() {}
		ShaderBufferLayout(const std::initializer_list<BufferElement>& elements)
			:m_Elements(elements)
		{
			CalculateOffsetAndStride();
		}

		inline const std::vector<BufferElement>& GetElements() const { return m_Elements; }
		inline const uint32_t& GetStride() const { return m_Stride; }

		std::vector<BufferElement>::const_iterator begin() const { return m_Elements.begin(); }
		std::vector<BufferElement>::const_iterator end()   const { return m_Elements.end(); }
		std::vector<BufferElement>::iterator begin() { return m_Elements.begin(); }
		std::vector<BufferElement>::iterator end() { return m_Elements.end(); }
	private:
		void CalculateOffsetAndStride()
		{
			uint32_t offset = 0;
			m_Stride = 0;
			for (auto& element : m_Elements)
			{
				element.Offset += offset;
				offset += element.Size;
				m_Stride += element.Size;
			}
		}
	private:
		std::vector<BufferElement> m_Elements;
		uint32_t m_Stride = 0;
	};
	class Shader : public RefCounter
	{
	public:
		virtual ~Shader() = default;

		virtual void Reload(bool forceCompile = false) = 0;
		virtual void RT_Reload(bool forceCompile) = 0;

		virtual const std::string& GetName() const = 0;

		virtual const ShaderBuffer& GetShaderBuffer() const = 0;
		virtual void SetShaderBuffer(ShaderBufferLayout layout)  = 0;

		static Ref<Shader> Create(const std::string& filepath, bool forceCompile = false, bool disableOptimization = false);
		//static Ref<Shader> CreateFromString(const std::string& source);
	
	};
	class ShaderLibrary : public RefCounter
	{

	public: 
		ShaderLibrary();
		~ShaderLibrary();
		void Add(Ref<Shader>& shader);
		void Add(const std::string& name, Ref<Shader>& shader);
		Ref<Shader> Load(const std::string& Filepath);
		Ref<Shader> Load(const std::string& name,const std::string& Filepath);

		Ref<Shader> Get(const std::string& name);
		bool Exists(const std::string& name) const;
	private:
		std::unordered_map<std::string, Ref<Shader>> m_Shaders;

	};

}