#include "kspch.h"
#include "OpenGLVertexArray.h"

#include "kans3D/Renderer/Shader.h"

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

		CORE_ASSERT(false, "unknown shaderdatatype��");
		return 0;

	}
	OpenGLVertexArray::OpenGLVertexArray()
	{
		HZ_PROFILE_FUCTION();
		//�˴�ͬ����Ҫ��Ⱦ��������ܼ�����ã���ʱʹ��HZ_PROFILE_FUCTION���в���
		glCreateVertexArrays(1, &m_RendererID);
	}

	OpenGLVertexArray::~OpenGLVertexArray()
	{
		HZ_PROFILE_FUCTION();

		glDeleteVertexArrays(1, &m_RendererID);
	}

	void OpenGLVertexArray::Bind() const
	{
		HZ_PROFILE_FUCTION();

		glBindVertexArray(m_RendererID);
	}

	void OpenGLVertexArray::Unbind() const
	{
		HZ_PROFILE_FUCTION();

		glBindVertexArray(0);
	}

	void OpenGLVertexArray::AddVertexBuffer(const Ref<VertexBuffer>& vertexBuffer) 
	{
		HZ_PROFILE_FUCTION();
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
		HZ_PROFILE_FUCTION();
		glBindVertexArray(m_RendererID);
		indexBuffer->Bind();
		m_IndexBuffers = indexBuffer;
	}

}