#pragma once

#include "RHI/OpenGL/VertexArray.h"
#include "Resource/Shader.h"
#include "Resource/Texture.h"
#include "Resource/SubTexture2D.h"
#include "Resource/Camera.h"
#include "Resource/OrthographicCamera.h"

namespace Kans {

	class Renderer2D {


	public:
		
		static void	Init();
		
		static void BeginScene(const OrthographicCamera& camera);
		static void BeginScene(const Camera& camera,const glm::mat4& view);

		static void EndScene();
		static void Shutdown();
		static void Flush();

		//简单图元的绘制，一个有z轴一个无z轴
		static void DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& tintcolor);
		static void DrawQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& tintcolor);
		static void DrawQuad(const glm::mat4& transform, const glm::vec4& tintcolor);


		static void DrawQuad(const glm::vec2& position, const glm::vec2& size, const Ref<Texture2D>& Texture2D, const glm::vec4& tintcolor = glm::vec4(1.0f), float tilingFactor = 1.0f);
		static void DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<Texture2D>& Texture2D, const glm::vec4& tintcolor = glm::vec4(1.0f), float tilingFactor = 1.0f);
		static void DrawQuad(const glm::mat4& transform, const Ref<Texture2D>& Texture2D, const glm::vec4& tintcolor = glm::vec4(1.0f), float tilingFactor = 1.0f);


		static void DrawQuad(const glm::vec2& position, const glm::vec2& size, const Ref<SubTexture2D>& subtexture2D, const glm::vec4& tintcolor = glm::vec4(1.0f), float tilingFactor = 1.0f);
		static void DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<SubTexture2D>& subtexture2D, const glm::vec4& tintcolor = glm::vec4(1.0f), float tilingFactor = 1.0f);
		static void DrawQuad(const glm::mat4& transform, const Ref<SubTexture2D>& subtexture2D, const glm::vec4& tintcolor = glm::vec4(1.0f), float tilingFactor = 1.0f);

		static void DrawRotateQuad(const glm::vec2& position, float roration, const glm::vec2& size, const glm::vec4& tintcolor);
		static void DrawRotateQuad(const glm::vec3& position, float roration, const glm::vec2& size, const glm::vec4& tintcolor);
		static void DrawRotateQuad(const glm::vec2& position, float roration, const glm::vec2& size, const Ref<Texture2D>& Texture2D, const glm::vec4& tintcolor = glm::vec4(1.0f),float tilingFactor = 1.0f);
		static void DrawRotateQuad(const glm::vec3& position, float roration, const glm::vec2& size, const Ref<Texture2D>& Texture2D, const glm::vec4& tintcolor = glm::vec4(1.0f),float tilingFactor = 1.0f);
		
		struct Statistics
		{
			uint32_t DrawCalls = 0;
			uint32_t QuadCount = 0;
			uint32_t GetTotalVertexCount() { return QuadCount * 4; }
			uint32_t GetTotalIndexCount() {  return QuadCount * 6; }
		};

		static void ResetStats();
		static Statistics& GetStats();
	private:
		static void FlushAndReset();
	};
}