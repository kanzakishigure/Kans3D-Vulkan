#pragma once
#include <glm/glm.hpp>
namespace Kans {

	enum class StencilFunction {
		NEVER ,
		LESS ,
		EQUAL,
		LEQUAL ,
		GREATER,
		NOTEQUAL,
		GEQUAL,
		ALWAYS
	}; 
	enum class StencilOption {
		KEEP,
		ZERO,
		REPLACE,
		INCR,
		INCR_WRAP,
		DECR,
		DECR_WRAP,
		INVERT
	};
	enum class CullFaceOption {
		BACK,
		FRONT,
		FRONT_AND_BACK
	};

	enum class RendererAPIType
	{
		NONE = 0, OPENGL = 1, Vulkan = 2
	};
	class RendererAPI
	{

	public:
		virtual void SetClearColor(const glm::vec4& color) = 0;
		virtual void Clear() = 0;
		virtual void SetViewPort(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;

		
		virtual void Init() = 0;

		static RendererAPIType GetAPIType() { return s_API; }
		static void SetAPI(RendererAPIType api);
		//TODO : Vulkan Renderer will Inherited this
	public:
		virtual void EnableDepthTest(bool enabled)=0;
		virtual void EnableSetStencil(bool enabled) =0;
		virtual void EnableCullFace(bool enabled) = 0;

		virtual void SetStencilMask(uint32_t mask) =0;
		virtual void SetStencilFunc(StencilFunction func, uint32_t value, uint32_t mask) = 0;
		virtual void StencilOp(StencilOption sfail, StencilOption dpfail, StencilOption dppass) = 0;
		virtual void CullFace(CullFaceOption option) =0;

		//Temp function
		virtual void BindTexture(uint32_t texture, uint32_t slot) = 0;
	private:
		inline static RendererAPIType s_API = RendererAPIType::NONE;

	};

	

}