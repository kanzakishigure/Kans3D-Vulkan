#include "kspch.h"
#include "OpenGLVertexArray.h"

#include "kans3D/Renderer/Resource/Shader.h"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

namespace Kans {

	static GLenum ShaderDataTypeToOpenGLBaseType(ShaderDataType type)
	{
		switch (type)
		{
		case ShaderDataType::Float:     return GL_FLOAT;
		case ShaderDataType::Float2:    return GL_FLOAT;
		case ShaderDataType::Float3:    return GL_FLOAT;
		case ShaderDataType::Float4:    return GL_FLOAT;
		case ShaderDataType::Mat3:      return GL_FLOAT;
		case ShaderDataType::Mat4:		return GL_FLOAT;
		case ShaderDataType::Int:		return GL_INT;
		case ShaderDataType::Int2:		return GL_INT;
		case ShaderDataType::Int3:		return GL_INT;
		case ShaderDataType::Int4:		return GL_INT;
		case ShaderDataType::Bool:		return GL_BOOL;
		}

		CORE_ASSERT(false, "unknown shaderdatatype！");
		return 0;

	}
	OpenGLVertexArray::OpenGLVertexArray()
	{
		PROFILE_FUCTION();
		//此处同样需要渲染级别的性能检测配置，暂时使用PROFILE_FUCTION进行测试
		glCreateVertexArrays(1, &m_RendererID);
	}

	OpenGLVertexArray::~OpenGLVertexArray()
	{
		PROFILE_FUCTION();

		glDeleteVertexArrays(1, &m_RendererID);
	}

	void OpenGLVertexArray::Bind() const
	{
		PROFILE_FUCTION();

		glBindVertexArray(m_RendererID);
	}

	void OpenGLVertexArray::Unbind() const
	{
		PROFILE_FUCTION();

		glBindVertexArray(0);
	}

	void OpenGLVertexArray::AddVertexBuffer(const Ref<VertexBuffer>& vertexBuffer) 
	{
		PROFILE_FUCTION();
		glBindVertexArray(m_RendererID);
		vertexBuffer->Bind();
		uint32_t index = 0;
		CLIENT_ASSERT(vertexBuffer->GetLayout().GetElements().size(),"VertexBuffer Layout is NULL")
		for (const auto& element : vertexBuffer->GetLayout())
		{
			glVertexAttribPointer(
				index,
				element.GetCompomentCount(),
				ShaderDataTypeToOpenGLBaseType(element.Type),
				element.Normalized ? GL_TRUE : GL_FALSE,
				vertexBuffer->GetLayout().GetStride(),
				(const void*)element.Offset);
			glEnableVertexAttribArray(index);
			index++;
		}
		m_VertexBuffers.push_back(vertexBuffer);
	}

	void OpenGLVertexArray::SetIndexBuffer(const Ref<IndexBuffer>& indexBuffer) 
	{
		PROFILE_FUCTION();
		glBindVertexArray(m_RendererID);
		indexBuffer->Bind();
		m_IndexBuffers = indexBuffer;
	}

}