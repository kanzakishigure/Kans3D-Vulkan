#pragma once
#include "Kans3D/Renderer/RHI/OpenGL/OpenGLRenderCommand.h"
#include "Kans3D/Renderer/RHI/RenderContext.h"

#include "Kans3D/Core/Window.h"

#include "Kans3D/Renderer/Shader.h"

//temp
#include "Kans3D/Renderer/Camera.h"
#include "Kans3D/Scene/Scene.h"
namespace Kans {
	

	class Renderer
	{
	public:
		
		static void	Init();
		static void Init(const Scope<Window>& window);
		static void RendererDataInit();
		static void	Shutdown();

		static void BeginScene(const Camera& camera, const glm::mat4& transform);
		static void EndScene();

		static void OnWindowResize(uint32_t width, uint32_t height);
		
		template<typename FuncT>
		static void Submit(FuncT&& func)
		{
			//just time we execute the function when we submit the function
			func();
		}
		
		static Ref<Texture2D> GetWhiteTexture();
		static Ref<Texture2D> GetBlackTexture();
		static Ref<ShaderLibrary> GetShaderLibrary();

		inline static RendererAPIType GetAPI() { return RendererAPI::GetAPIType(); }
	};
	
}