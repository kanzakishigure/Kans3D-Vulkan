#pragma once
#include "Kans3D/Renderer/RHI/RendererAPI.h"
#include "Kans3D/Renderer/RHI/OpenGL/VertexArray.h"
namespace Kans {
	
	class OpenGLRendererAPI : public RendererAPI
	{
	public:
		virtual void SetClearColor(const glm::vec4& color) override;
		virtual void Clear() override;
		virtual void Init() override;
		

		virtual void SetViewPort(uint32_t x, uint32_t y, uint32_t width, uint32_t height)override;

		virtual void EnableDepthTest(bool enabled) override;
		virtual void EnableSetStencil(bool enabled) override;
		virtual void EnableCullFace(bool enabled) override;

		virtual void SetStencilMask(uint32_t mask) override;
		virtual void SetStencilFunc(StencilFunction func, uint32_t value, uint32_t mask) override;
		virtual void StencilOp(StencilOption sfail, StencilOption dpfail, StencilOption dppass) override;
		virtual void CullFace(CullFaceOption option) override;

		virtual void BindTexture(uint32_t texture, uint32_t slot) override;


		virtual void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0);
		virtual void DrawArray(const Ref<VertexArray>& vertexArray, uint32_t vertexCount = 0);
	};


}