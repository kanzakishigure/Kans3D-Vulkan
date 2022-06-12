#pragma once
#include "RenderCommand.h"
#include "Kans3D/Renderer/OrthographicCamera.h"
#include "Kans3D/Renderer/Shader.h"
#include "Kans3D/Renderer/Mesh.h"
#include "Kans3D/Renderer/Camera.h"
//test
#include "Kans3D/Scene/Scene.h"
namespace Kans {
	

	class Renderer
	{
	public:
		
		static void	Init();
		static void RendererDataInit();
		static void	Shutdown();
	public:
		static void BeginScene(const Camera& camera, const glm::mat4& transform);
		static void EndScene();

		static void Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const glm::mat4& transform);
		static void OnWindowResize(uint32_t width, uint32_t height);

		static Ref<Texture2D> GetWhiteTexture();
		static Ref<Texture2D> GetBlackTexture();
		static Ref<ShaderLibrary> GetShaderLibrary();

		inline static RendererAPIType GetAPI() { return RendererAPI::GetAPI(); }
	};
	
}