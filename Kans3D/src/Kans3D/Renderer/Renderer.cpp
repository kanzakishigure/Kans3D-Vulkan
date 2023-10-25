#include "kspch.h"
#include "Renderer.h"
#include "Renderer2D.h"
#include "Resource/SceneCamera.h"
#include "RHI/RHI.h"

#include "Kans3D/Platform/OpenGL/OpenGLShader.h"
#include "Kans3D/Platform/OpenGL/OpenGLRendererAPI.h"
#include "Kans3D/Platform/Vulkan/ShaderCompiler/VulkanShaderCompiler.h"
#include "Kans3D/FileSystem/FileSystem.h"


namespace Kans {
	
	

	Ref<RHI> Renderer::s_RHI = nullptr;
	static RendererAPI* s_RendererAPI = nullptr;

	
	struct GlobalRendererResource
	{
		glm::mat4 ViewProjectionMatix;

		Ref<Texture2D> WhiteTexture;
		Ref<Texture2D> BlackTexture;
		Ref<ShaderLibrary> m_ShaderLibrary;

		std::unordered_map<std::string, std::string> GlobalShaderMacros;

	};
	static GlobalRendererResource* s_GlobalRendererResource = nullptr;

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

		Renderer::InitGlobalRendererResource();

		//we need support Vulkan, so need change the architecture, just temp 
		if (RendererAPI::Current() == RendererAPIType::OPENGL)
			OpenGLRenderCommand::s_RendererAPI = s_RendererAPI;

		
	}

	void Renderer::Init(const Scope<Window>& window)
	{

		

		s_RHI = RHI::Create(window);
		s_RHI->Init();
		//we need support Vulkan, so need change the architecture, just temp for now
		if (RendererAPI::Current() == RendererAPIType::OPENGL)
		{
			s_RendererAPI = InitRendererAPI();
			s_RendererAPI->Init();
			OpenGLRenderCommand::s_RendererAPI = s_RendererAPI;
		}

		s_GlobalRendererResource = DEBUG_NEW GlobalRendererResource();
		s_GlobalRendererResource->m_ShaderLibrary = CreateRef<ShaderLibrary>();
		Renderer::InitGlobalRendererResource();
		
	}

	void Renderer::InitGlobalRendererResource()
	{
		
		//Init renderer asset
		//Texture
		{
			TextureSpecification spec;
			spec.Width = 1;
			spec.Height = 1;
			spec.Format = RHIFormat::RHI_FORMAT_R8G8B8A8_SRGB;

			uint32_t Wightdata = 0xffffffff;
			s_GlobalRendererResource->WhiteTexture = Texture2D::Create(spec, Buffer(&Wightdata, sizeof(uint32_t)));

			uint32_t Blackdata = 0xff000000;
			s_GlobalRendererResource->BlackTexture = Texture2D::Create(spec, Buffer(&Blackdata, sizeof(uint32_t)));

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
				s_GlobalRendererResource->m_ShaderLibrary->Add(StaticShader);
			}

			{
				auto StencilShader = Shader::Create(shaderpath + "StencilShader.glsl");
				StencilShader->SetShaderBuffer({});
				s_GlobalRendererResource->m_ShaderLibrary->Add(StencilShader);
			}

			{
				auto PostShader = Shader::Create(shaderpath + "PostShader.glsl");
				PostShader->SetShaderBuffer({});
				s_GlobalRendererResource->m_ShaderLibrary->Add(PostShader);
			}
			
			{
				auto OutLineShader = Shader::Create(shaderpath + "OutLineShader.glsl");
				OutLineShader->SetShaderBuffer({});
				s_GlobalRendererResource->m_ShaderLibrary->Add(OutLineShader);
				
			}

			{
				auto ToneShader = Shader::Create(shaderpath + "ToneShader.glsl");
				ToneShader->SetShaderBuffer({
						{ShaderDataType::Float3,MaterialAsset::GetDiffuseLocation()},
						{ShaderDataType::Float3,MaterialAsset::GetSpecularLocation()},
						{ShaderDataType::Float3,MaterialAsset::GetEmissionLocation()},
						{ShaderDataType::Float, MaterialAsset::GetShininessLocation()}
					});
				s_GlobalRendererResource->m_ShaderLibrary->Add(ToneShader);
			}

			{
				auto DebugShader = Shader::Create(shaderpath + "DebugShader.glsl");
				DebugShader->SetShaderBuffer({
						{ShaderDataType::Float,MaterialAsset::GetShininessLocation()},
						{ShaderDataType::Float,"U_Debug"},
						{ShaderDataType::Color3,"U_DarkColor"},
						{ShaderDataType::Color4,"U_DarkColor4"},

					});
				s_GlobalRendererResource->m_ShaderLibrary->Add(DebugShader);
			}
			

			{
				auto DebugnormalShader = Shader::Create(shaderpath + "DebugNormalShader.glsl");
				DebugnormalShader->SetShaderBuffer({
					{ShaderDataType::Color4,"u_NormalColor"}
					});
				s_GlobalRendererResource->m_ShaderLibrary->Add(DebugnormalShader);
			}
			
			{
				auto ToneCharactorShader = Shader::Create(shaderpath + "GenShin_Shader/ToneCharactorShader.glsl");
				ToneCharactorShader->SetShaderBuffer({
					{ShaderDataType::Float,"U_RampShadowRange"},
					{ShaderDataType::Float,"U_RampAOShadowRange"},
					{ShaderDataType::Float,"U_ShadowIntensity"},
					{ShaderDataType::Float,"U_BrightIntensity"},
					{ShaderDataType::Float,"U_AOIntensity"},
					{ShaderDataType::Float,"U_CharacterIntensity"},
					{ShaderDataType::Float,"U_RampIntensity"},

					{ShaderDataType::Float,"U_SpecularThreshold_1"},
					{ShaderDataType::Float,"U_SpecularIntensity_1"},
					{ShaderDataType::Float,"U_SpecularThreshold_2"},
					{ShaderDataType::Float,"U_SpecularIntensity_2"},
					{ShaderDataType::Float,"U_SpecularThreshold_3"},
					{ShaderDataType::Float,"U_SpecularIntensity_3"},



					{ShaderDataType::Bool,"_UseRamp"},
					
					});
				s_GlobalRendererResource->m_ShaderLibrary->Add(ToneCharactorShader);
			}


			{
				auto StandardShader = Shader::Create(shaderpath + "StandardShader.glsl");
				StandardShader->SetShaderBuffer({
					{ShaderDataType::Color4,"material.U_Color"}
					
					});
				s_GlobalRendererResource->m_ShaderLibrary->Add(StandardShader);
			}
				
			{
				auto SpotCloudShader = Shader::Create(shaderpath + "SpotCloud/SpotCloudShader.glsl");
				SpotCloudShader->SetShaderBuffer({});
				s_GlobalRendererResource->m_ShaderLibrary->Add(SpotCloudShader);

				SpotCloudShader->SetShaderBuffer({
						{ShaderDataType::Float,"U_PerlinBias"},
						{ShaderDataType::Float3,"U_Effect"},

					});
			}

			{
				auto pbrShader = Shader::Create(shaderpath + "PBR/pbrShader.glsl");
				pbrShader->SetShaderBuffer({});
				s_GlobalRendererResource->m_ShaderLibrary->Add(pbrShader);
			}

			{
				auto skyboxShader = Shader::Create(shaderpath + "PBR/skybox.glsl");
				skyboxShader->SetShaderBuffer({});
				s_GlobalRendererResource->m_ShaderLibrary->Add(skyboxShader);
			}

			{
				auto HdrToCubeMap = Shader::Create(shaderpath + "PBR/HdrToCubeMap.glsl");
				HdrToCubeMap->SetShaderBuffer({});
				s_GlobalRendererResource->m_ShaderLibrary->Add(HdrToCubeMap);
			}
			{
				auto prtShader = Shader::Create(shaderpath + "PBR/prt.glsl");
				prtShader->SetShaderBuffer({});
				s_GlobalRendererResource->m_ShaderLibrary->Add(prtShader);
			}
			{
				auto prefilterShader = Shader::Create(shaderpath + "PBR/prefilter.glsl");
				prefilterShader->SetShaderBuffer({});
				s_GlobalRendererResource->m_ShaderLibrary->Add(prefilterShader);
			}
			{
				auto brdfShader = Shader::Create(shaderpath + "PBR/brdf.glsl");
				brdfShader->SetShaderBuffer({});
				s_GlobalRendererResource->m_ShaderLibrary->Add(brdfShader);
			}
			{
				auto pbrTextureShader = Shader::Create(shaderpath + "PBR/pbrTextureShader.glsl");
				pbrTextureShader->SetShaderBuffer({});
				s_GlobalRendererResource->m_ShaderLibrary->Add(pbrTextureShader);
				
			}
		}

	}

	void Renderer::Shutdown()
	{
		delete s_GlobalRendererResource;
		CORE_INFO("delete rendererdata");
		delete s_RendererAPI;
		CORE_INFO("delete renderer-rhi");
		
		s_RHI->Shutdown();
		s_RHI.reset();

	}

	void Renderer::BeginScene(const Camera& camera, const glm::mat4& transform)
	{
		
		s_GlobalRendererResource->ViewProjectionMatix = camera.GetProjectMatrix()*glm::inverse(transform);
		
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
		return s_GlobalRendererResource->WhiteTexture;
	}

	Ref<Texture2D> Renderer::GetBlackTexture()
	{
		return s_GlobalRendererResource->BlackTexture;
	}


	Ref<ShaderLibrary> Renderer::GetShaderLibrary()
	{
		return s_GlobalRendererResource->m_ShaderLibrary;
	}


	const std::unordered_map<std::string, std::string>& Renderer::GetGlobalShaderMacros()
	{
		return s_GlobalRendererResource->GlobalShaderMacros;
	}

}