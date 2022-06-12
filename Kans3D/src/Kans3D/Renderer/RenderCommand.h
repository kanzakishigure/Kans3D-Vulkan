#pragma once
#include "RendererAPI.h"
namespace Kans {

	class RenderCommand
	{
	public:
		//RenderCommand内部应该满足单一职责原则，不应该实现多余的功能，导致过藕合
		inline static void SetClearColor(const glm::vec4& color) 
		{
			s_RendererAPI->SetClearColor(color);
		}
		inline static void Clear() 
		{	
			s_RendererAPI->Clear();
		}
		inline static void  DrawIndexed(const Ref<VertexArray>& vertexArray,uint32_t indexCount=0)
		{
			s_RendererAPI->DrawIndexed(vertexArray,indexCount);
		}
		inline static void Init() 
		{
			s_RendererAPI->Init();
		}
		inline static void SetViewPort(uint32_t x, uint32_t y,uint32_t width, uint32_t height)
		{
			s_RendererAPI->SetViewPort(x, y, width, height);
		}
		inline static void EnableSetStencil(bool enabled)
		{
			s_RendererAPI->EnableSetStencil(enabled);
		}
		inline static void EnableDepthTest(bool enabled)
		{
			s_RendererAPI->EnableDepthTest(enabled);
		}
		inline static void SetStencilFunc(StencilFunction func, uint32_t value, uint32_t mask)
		{
			s_RendererAPI->SetStencilFunc(func,value,mask);
		}
		inline static void SetStencilMask(uint32_t mask) 
		{
			s_RendererAPI->SetStencilMask(mask);
		}
		inline static void StencilOp(StencilOption sfail, StencilOption dpfail, StencilOption dppass)
		{
			s_RendererAPI->StencilOp(sfail, dpfail, dppass);
		}
		inline static void EnableCullFace(bool enabled)
		{
			s_RendererAPI->EnableCullFace(enabled);
		}
		inline static void CullFace(CullFaceOption option)
		{
			s_RendererAPI->CullFace(option);
		}
		inline static void BindTexture(uint32_t texture, uint32_t slot)
		{
			s_RendererAPI->BindTexture(texture,slot);
		}
	private:
		 inline static RendererAPI* s_RendererAPI;

		 friend class Renderer;
	};

}