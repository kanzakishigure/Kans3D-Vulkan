#pragma once
#include "Kans3D/Renderer/RHI/Buffer.h"
namespace Kans {

	class OpenGLVertexBuffer :public VertexBuffer
	{
	public:
		OpenGLVertexBuffer(float* vertices, uint32_t size);
		OpenGLVertexBuffer(uint32_t size);

		virtual ~OpenGLVertexBuffer();
		virtual void Bind() const override;
		virtual void Unbind() const override;
		
		virtual void SetData(const void* data, uint32_t  size) override;

		virtual const BufferLayout& GetLayout() const override { return m_Layout; }
		virtual void SetBufferLayout(const BufferLayout& layout)  override { m_Layout = layout; }
	private:
		uint32_t m_RendererID;
		BufferLayout m_Layout;
	};


	class OpenGLIndexBuffer :public IndexBuffer
	{
	public:
		OpenGLIndexBuffer(uint32_t* indices, uint32_t size);
		virtual ~OpenGLIndexBuffer();
		virtual void Bind() const;
		virtual void Unbind() const;
		virtual uint32_t GetCount()const { return m_Count; }
	private:
		uint32_t m_RendererID;
		uint32_t m_Count;
	};
}