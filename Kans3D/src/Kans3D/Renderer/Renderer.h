#pragma once
#include "Kans3D/Core/Window.h"
#include "Kans3D/Renderer/RHI/OpenGL/OpenGLRenderCommand.h"
#include "Kans3D/Renderer/RHI/RHI.h"
#include "Kans3D/Renderer/RenderPipeline.h"

#include "Resource/Camera.h"
#include "Resource/Texture.h"
#include "Resource/Shader.h"

namespace Kans {
	
	
	class Texture2D;
	class ShaderLibrary;
	struct GlobalRendererResource
	{
		glm::mat4 ViewProjectionMatix;

		Ref<VertexArray> Quad;
		Ref<Texture2D> WhiteTexture;
		Ref<Texture2D> BlackTexture;
		Ref<ShaderLibrary> m_ShaderLibrary;

		std::unordered_map<std::string, std::string> GlobalShaderMacros;

	};
	
	class Renderer
	{
	public:
		

		static Ref<RHI> GetRHI() { return s_RHI; }
		static void	Init();
		static void Init(const Scope<Window>& window);
		static void InitGlobalRendererResource();

		static void InitRenderPipline(const RenderPipelineSpecification& spec);

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
		static Ref<VertexArray> GetQuad();
		static Ref<ShaderLibrary> GetShaderLibrary();
		
		// Add macro from shader.
		static const std::unordered_map<std::string, std::string>& GetGlobalShaderMacros();

	private:
		static Ref<RHI> s_RHI;
		static Ref<RenderPipeline> s_RenderPipeline;
		
	};
	
}