#include "kspch.h"
#include "Renderer.h"
#include "Renderer2D.h"
#include "Platform/OpenGL/OpenGLShader.h"
#include "Kans3D/Renderer/SceneCamera.h"
#include "Platform/OpenGL/OpenGLRendererAPI.h"
#include "Kans3D/Renderer/RHI/RHI.h"

#include "Kans3D/FileSystem/FileSystem.h"
#include "Platform/Vulkan/ShaderCompiler/VulkanShaderCompiler.h"

namespace Kans {
	
	

	Ref<RHI> Renderer::s_RHI = nullptr;
	static RendererAPI* s_RendererAPI = nullptr;

	struct RendererData
	{
		glm::mat4 ViewProjectionMatix;

		Ref<Texture2D> WhiteTexture;
		Ref<Texture2D> BlackTexture;
		Ref<ShaderLibrary> m_ShaderLibrary;

		std::unordered_map<std::string, std::string> GlobalShaderMacros;

	};
	static RendererData* s_RendererData = nullptr;

	static RendererAPI* InitRendererAPI()
	{
		switch (RendererAPI::Current())
		{
		case RendererAPIType::Vulkan: return  nullptr;
		case RendererAPIType::OPENGL: return	new OpenGLRendererAPI();
		}
		CORE_ASSERT(false, "Unknown RendererAPI");
		return nullptr;
	}
	void Renderer::Init()
	{
		HZ_PROFILE_FUCTION();

		
		s_RHI = RHI::Create();
		s_RHI->Init();

		s_RendererAPI = InitRendererAPI();
		s_RendererAPI->Init();

		Renderer::RendererDataInit();

		//we need support Vulkan, so need change the architecture, just temp 
		if (RendererAPI::Current() == RendererAPIType::OPENGL)
			OpenGLRenderCommand::s_RendererAPI = s_RendererAPI;

		
	}

	void Renderer::Init(const Scope<Window>& window)
	{

		

		s_RHI = RHI::Create(window);
		s_RHI->Init();
		//we need support Vulkan, so need change the architecture, just temp 
		if (RendererAPI::Current() == RendererAPIType::OPENGL)
		{
			s_RendererAPI = InitRendererAPI();
			s_RendererAPI->Init();
			OpenGLRenderCommand::s_RendererAPI = s_RendererAPI;
		}

		s_RendererData = DEBUG_NEW RendererData();
		s_RendererData->m_ShaderLibrary = CreateRef<ShaderLibrary>();
		Renderer::RendererDataInit();
		
	}

	void Renderer::RendererDataInit()
	{
		
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
		//TODO technically we can use SPIR-V reflect the Vulkan-GLSL layout to get the uniformStorgeBuffer
		{
			std::string& shaderpath = KansFileSystem::GetShaderFolder().generic_string() + "/";
			{
				
				auto StaticShader = Shader::Create(shaderpath + "StaticMeshShader.glsl");
				StaticShader->SetShaderBuffer({
						{ShaderDataType::Float, MaterialAsset::GetShininessLocation()},
						
					});
				s_RendererData->m_ShaderLibrary->Add(StaticShader);
			}

			{
				auto StencilShader = Shader::Create(shaderpath + "StencilShader.glsl");
				StencilShader->SetShaderBuffer({});
				s_RendererData->m_ShaderLibrary->Add(StencilShader);
			}

			{
				auto PostShader = Shader::Create(shaderpath + "PostShader.glsl");
				PostShader->SetShaderBuffer({});
				s_RendererData->m_ShaderLibrary->Add(PostShader);
			}
			
			{
				auto OutLineShader = Shader::Create(shaderpath + "OutLineShader.glsl");
				OutLineShader->SetShaderBuffer({});
				s_RendererData->m_ShaderLibrary->Add(OutLineShader);
				
			}

			{
				auto ToneShader = Shader::Create(shaderpath + "ToneShader.glsl");
				ToneShader->SetShaderBuffer({
						{ShaderDataType::Float3,MaterialAsset::GetDiffuseLocation()},
						{ShaderDataType::Float3,MaterialAsset::GetSpecularLocation()},
						{ShaderDataType::Float3,MaterialAsset::GetEmissionLocation()},
						{ShaderDataType::Float, MaterialAsset::GetShininessLocation()}
					});
				s_RendererData->m_ShaderLibrary->Add(ToneShader);
			}

			{
				auto DebugShader = Shader::Create(shaderpath + "DebugShader.glsl");
				DebugShader->SetShaderBuffer({
						{ShaderDataType::Float,MaterialAsset::GetShininessLocation()},
						{ShaderDataType::Float,"U_Debug"},
						{ShaderDataType::Color3,"U_DarkColor"},
						{ShaderDataType::Color4,"U_DarkColor4"},

					});
				s_RendererData->m_ShaderLibrary->Add(DebugShader);
			}
			

			{
				auto DebugnormalShader = Shader::Create(shaderpath + "DebugNormalShader.glsl");
				DebugnormalShader->SetShaderBuffer({
					{ShaderDataType::Color4,"u_NormalColor"}
					});
				s_RendererData->m_ShaderLibrary->Add(DebugnormalShader);
			}
			
			{
				auto ToneCharactorShader = Shader::Create(shaderpath + "GenShin_Shader/ToneCharactorShader.glsl");
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
				auto StandardShader = Shader::Create(shaderpath + "StandardShader.glsl");
				StandardShader->SetShaderBuffer({
					{ShaderDataType::Color4,"material.U_Color"}
					
					});
				s_RendererData->m_ShaderLibrary->Add(StandardShader);
			}
				
			{
				auto SpotCloudShader = Shader::Create(shaderpath + "SpotCloud/SpotCloudShader.glsl");
				SpotCloudShader->SetShaderBuffer({});
				s_RendererData->m_ShaderLibrary->Add(SpotCloudShader);

				SpotCloudShader->SetShaderBuffer({
						{ShaderDataType::Float,"U_PerlinBias"},
						{ShaderDataType::Float3,"U_Effect"},

					});
			}

			{
				auto pbrShader = Shader::Create(shaderpath + "PBR/pbrShader.glsl");
				pbrShader->SetShaderBuffer({});
				s_RendererData->m_ShaderLibrary->Add(pbrShader);
			}

			{
				auto skyboxShader = Shader::Create(shaderpath + "PBR/skybox.glsl");
				skyboxShader->SetShaderBuffer({});
				s_RendererData->m_ShaderLibrary->Add(skyboxShader);
			}

			{
				auto HdrToCubeMap = Shader::Create(shaderpath + "PBR/HdrToCubeMap.glsl");
				HdrToCubeMap->SetShaderBuffer({});
				s_RendererData->m_ShaderLibrary->Add(HdrToCubeMap);
			}
			{
				auto prtShader = Shader::Create(shaderpath + "PBR/prt.glsl");
				prtShader->SetShaderBuffer({});
				s_RendererData->m_ShaderLibrary->Add(prtShader);
			}
			{
				auto prefilterShader = Shader::Create(shaderpath + "PBR/prefilter.glsl");
				prefilterShader->SetShaderBuffer({});
				s_RendererData->m_ShaderLibrary->Add(prefilterShader);
			}
			{
				auto brdfShader = Shader::Create(shaderpath + "PBR/brdf.glsl");
				brdfShader->SetShaderBuffer({});
				s_RendererData->m_ShaderLibrary->Add(brdfShader);
			}
			{
				auto pbrTextureShader = Shader::Create(shaderpath + "PBR/pbrTextureShader.glsl");
				pbrTextureShader->SetShaderBuffer({});
				s_RendererData->m_ShaderLibrary->Add(pbrTextureShader);
				
			}
		}

	}

	void Renderer::Shutdown()
	{
		delete s_RendererData;
		CORE_INFO("delete rendererdata");
		delete s_RendererAPI;
		CORE_INFO("delete renderer-rhi");
		
		s_RHI->Shutdown();
		s_RHI.reset();

	}

	void Renderer::BeginScene(const Camera& camera, const glm::mat4& transform)
	{
		
		s_RendererData->ViewProjectionMatix = camera.GetProjectMatrix()*glm::inverse(transform);
		
	}
	void Renderer::EndScene()
	{

	}

	void Renderer::OnWindowResize(uint32_t width, uint32_t height)
	{
		s_RendererAPI->SetViewPort(0, 0, width, height);
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


	const std::unordered_map<std::string, std::string>& Renderer::GetGlobalShaderMacros()
	{
		return s_RendererData->GlobalShaderMacros;
	}

}