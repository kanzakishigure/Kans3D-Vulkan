#include "kspch.h"
#include "Renderer.h"
#include "Renderer2D.h"
#include "Platform/OpenGL/OpenGLShader.h"
#include "Kans3D/Renderer/SceneCamera.h"
#include "Platform/OpenGL/OpenGLRendererAPI.h"

#include "Kans3D/FileSystem/FileSystem.h"
namespace Kans {
	
	
	struct RendererData
	{
		glm::mat4 ViewProjectionMatix;

		Ref<Texture2D> WhiteTexture;
		Ref<Texture2D> BlackTexture;
		Ref<ShaderLibrary> m_ShaderLibrary;

	};
	static RendererData* s_RendererData = nullptr;

	static RendererAPI* InitRendererAPI()
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPIType::Vulkan: return  nullptr;
		case RendererAPIType::OPENGL: return	new OpenGLRendererAPI();
		}
		HZ_CORE_ASSERT(false, "Unknown RendererAPI");
		return nullptr;
	}
	void Renderer::Init()
	{
		HZ_PROFILE_FUCTION();
		Renderer::RendererDataInit();
		RenderCommand::s_RendererAPI = InitRendererAPI();
		RenderCommand::Init();
		Renderer2D::Init();
	}

	void Renderer::RendererDataInit()
	{
		s_RendererData = new RendererData();
		s_RendererData->m_ShaderLibrary = CreateRef<ShaderLibrary>();
		//init renderer asset
		//Texture
		{
			s_RendererData->WhiteTexture = Texture2D::Create(1, 1);
			uint32_t data = 0xffffffff;
			s_RendererData->WhiteTexture->SetData((void*)&data, sizeof(uint32_t));

			s_RendererData->BlackTexture = Texture2D::Create(1, 1);
			data = 0xff000000;
			s_RendererData->BlackTexture->SetData((void*)&data, sizeof(uint32_t));
		}
		//shader
		//TODO technically we can use spr-v reflect the vulkan-glsl pushconst to get the UniformStorgeBuffer
		{
			std::string& shaderpath = KansFileSystem::GetShaderFolder().generic_string();
			{
				
				auto StaticShader = Shader::Create(shaderpath + "/StaticMeshShader.glsl");
				StaticShader->SetShaderBuffer({
						{ShaderDataType::Float3,MaterialAsset::GetDiffuseLocation()},
						{ShaderDataType::Float3,MaterialAsset::GetSpecularLocation()},
						{ShaderDataType::Float3,MaterialAsset::GetEmissionLocation()},
						{ShaderDataType::Float, MaterialAsset::GetShininessLocation()},
						{ShaderDataType::Float3,"dirLight.LightDir"},
						{ShaderDataType::Float3,"dirLight.Ambient_Intensity"},
						{ShaderDataType::Float3,"dirLight.Diffuse_Intensity"},
						{ShaderDataType::Float3,"dirLight.Specular_Intensity"},
						{ShaderDataType::Float3,"pointLight.Position"},
						{ShaderDataType::Float3,"pointLight.Ambient_Intensity"},
						{ShaderDataType::Float3,"pointLight.Diffuse_Intensity"},
						{ShaderDataType::Float3,"pointLight.Specular_Intensity"},
						{ShaderDataType::Float3,"U_ViewPos"}
					});
				s_RendererData->m_ShaderLibrary->Add(StaticShader);
			}

			{
				auto StencilShader = Shader::Create(shaderpath + "/StencilShader.glsl");
				StencilShader->SetShaderBuffer({});
				s_RendererData->m_ShaderLibrary->Add(StencilShader);
			}

			{
				auto PostShader = Shader::Create(shaderpath + "/PostShader.glsl");
				PostShader->SetShaderBuffer({});
				s_RendererData->m_ShaderLibrary->Add(PostShader);
			}
			
			{
				auto OutLineShader = Shader::Create(shaderpath + "/OutLineShader.glsl");
				OutLineShader->SetShaderBuffer({});
				s_RendererData->m_ShaderLibrary->Add(OutLineShader);
			}

			{
				auto ToneShader = Shader::Create(shaderpath + "/ToneShader.glsl");
				ToneShader->SetShaderBuffer({
						{ShaderDataType::Float3,MaterialAsset::GetDiffuseLocation()},
						{ShaderDataType::Float3,MaterialAsset::GetSpecularLocation()},
						{ShaderDataType::Float3,MaterialAsset::GetEmissionLocation()},
						{ShaderDataType::Float, MaterialAsset::GetShininessLocation()}
					});
				s_RendererData->m_ShaderLibrary->Add(ToneShader);
			}

			{
				auto DebugShader = Shader::Create(shaderpath + "/DebugShader.glsl");
				DebugShader->SetShaderBuffer({
						{ShaderDataType::Float,MaterialAsset::GetShininessLocation()},
						{ShaderDataType::Float,"U_Debug"},
						{ShaderDataType::Color3,"U_DarkColor"},
						{ShaderDataType::Color4,"U_DarkColor4"},

					});
				s_RendererData->m_ShaderLibrary->Add(DebugShader);
			}
			

			{
				auto DebugnormalShader = Shader::Create(shaderpath + "/DebugNormalShader.glsl");
				DebugnormalShader->SetShaderBuffer({
					});
				s_RendererData->m_ShaderLibrary->Add(DebugnormalShader);
			}
			
			{
				auto ToneCharactorShader = Shader::Create(shaderpath + "/GenShin_Shader/ToneCharactorShader.glsl");
				ToneCharactorShader->SetShaderBuffer({
					{ShaderDataType::Color4,"U_ShadowMultColor"},
					{ShaderDataType::Color4,"U_DarkShadowMultColor"},
					{ShaderDataType::Float,"U_ShadowArea"},
					{ShaderDataType::Float,"U_FixDarkShadow"},
					{ShaderDataType::Float,"U_ShadowSmooth"},
					{ShaderDataType::Float,"U_DarkShadowSmooth"},
					{ShaderDataType::Float,"U_DarkShadowArea"},
					{ShaderDataType::Float,"U_RampShadowRange"},
					{ShaderDataType::Bool,"U_UseRamp"},
					{ShaderDataType::Float2,"U_RampSkin"},
					{ShaderDataType::Float2,"U_RampTights"},
					{ShaderDataType::Float2,"U_RampMetal"},
					{ShaderDataType::Float2,"U_RampSoft"},
					{ShaderDataType::Float2,"U_RampSilk"}
					});
				s_RendererData->m_ShaderLibrary->Add(ToneCharactorShader);
			}


			{
				auto StandardShader = Shader::Create(shaderpath + "/StandardShader.glsl");
				StandardShader->SetShaderBuffer({
					{ShaderDataType::Color4,"material.U_Color"}
					
					});
				s_RendererData->m_ShaderLibrary->Add(StandardShader);
			}
				
			{
				auto SpotCloudShader = Shader::Create(shaderpath + "/SpotCloud/SpotCloudShader.glsl");
				SpotCloudShader->SetShaderBuffer({});
				s_RendererData->m_ShaderLibrary->Add(SpotCloudShader);

				SpotCloudShader->SetShaderBuffer({
						{ShaderDataType::Float,"U_PerlinBias"},
						{ShaderDataType::Float3,"U_Effect"},

					});
			}
		}

	}

	void Renderer::Shutdown()
	{
		delete s_RendererData;
		HZ_INFO("delete rendererdata");
		delete RenderCommand::s_RendererAPI;
		HZ_INFO("delete renderer-rhi");
	}

	void Renderer::BeginScene(const Camera& camera, const glm::mat4& transform)
	{
		
		s_RendererData->ViewProjectionMatix = camera.GetProjectMatrix()*glm::inverse(transform);
		
	}
	void Renderer::EndScene()
	{

	}

	void Renderer::Submit(const Ref<Shader>& shader,const Ref<VertexArray>& vertexArray,const glm::mat4& transform)
	{
		shader->Bind();
		std::dynamic_pointer_cast<OpenGLShader>(shader)->UploadUniformMat4("U_ViewProjection", s_RendererData->ViewProjectionMatix);
		std::dynamic_pointer_cast<OpenGLShader>(shader)->UploadUniformMat4("U_Transform", transform);
		vertexArray->Bind();
		RenderCommand::DrawIndexed(vertexArray);
	}

	void Renderer::OnWindowResize(uint32_t width, uint32_t height)
	{
		RenderCommand::SetViewPort(0, 0, width, height);
	}

	Ref<Texture2D> Renderer::GetWhiteTexture()
	{
		return s_RendererData->WhiteTexture;
	}

	Ref<Texture2D> Renderer::GetBlackTexture()
	{
		return s_RendererData->BlackTexture;
	}


	Ref<ShaderLibrary> Renderer::GetShaderLibrary()
	{
		return s_RendererData->m_ShaderLibrary;
	}


}