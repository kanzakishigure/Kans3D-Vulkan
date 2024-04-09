#include "kspch.h"
#include "Kans3D/Renderer/SceneRenderer.h"
#include "Kans3D/Renderer/Renderer.h"
//test
#include "imgui.h"
//temp
#include "Kans3D/Platform/OpenGL/OpenGLShader.h"
#include "Kans3D/Renderer/Resource/MeshFactory.h"
#include "Kans3D/Core/Application.h"
#define  EnableEnv true

namespace Kans 
{

	SceneRenderer::SceneRenderer(RenderScene* scene, SceneRendererSpecification spec)
		:m_RenderScene(scene),m_Specification(spec)
	{
		
		Init();
	}

	void SceneRenderer::Init()
	{
		m_ViewportWidth = m_RenderScene->m_ViewportWidth;
		m_ViewportHeight = m_RenderScene->m_ViewportHeight; 
		//FrameBuffer init
		{

			FrameBufferSpecification spec;
			spec.Width = m_ViewportWidth;
			spec.Height = m_ViewportHeight;
			spec.SwapChainTarget = true;
			//Color                           normal													Depth
			spec.AttachmentSpecification = { FrameBufferTextureFormat::RGBA8 ,FrameBufferTextureFormat::RGBA8,FrameBufferTextureFormat::Depth };
			m_FrameBuffer = FrameBuffer::Create(spec);
			m_FrameBuffer->Unbind();
		}
		
		
	}

	uint32_t SceneRenderer::GetOutput(uint32_t index) const
	{


		if (index > m_FrameBuffer->GetColorAttachmentCount() + 1)
			return -1; // Invalid index
		if (index < m_FrameBuffer->GetColorAttachmentCount())
			return m_FrameBuffer->GetColorAttachmentRendererID(index);
		return m_FrameBuffer->GetDepthAttachmentRendererID();
	}

	void SceneRenderer::PrepareEnvironment()
	{
		//prepareSceneEnv
		OpenGLRenderCommand::EnableCullFace(false);
		OpenGLRenderCommand::Clear();

		{
			FrameBufferSpecification frameBufferSpec;
			frameBufferSpec.Width = 512;
			frameBufferSpec.Height = 512;
			frameBufferSpec.SwapChainTarget = true;
			frameBufferSpec.AttachmentSpecification = { FrameBufferTextureFormat::RGB16F,FrameBufferTextureFormat::Depth };

			Ref<FrameBuffer> framebuffer = FrameBuffer::Create(frameBufferSpec);


			TextureSpecification textureSepc;
			textureSepc.Wrap = RHISamplerAddressMode::RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			textureSepc.Minf = RHIFilter::RHI_FILTER_LINEAR;
			textureSepc.Maxf = RHIFilter::RHI_FILTER_LINEAR;


			Ref<Texture2D> hdrTexture = Texture2D::Create(textureSepc, m_RenderScene->m_EnvironmentPath);

			// 设置cubemap6个面的viewmatrix
			// ----------------------------------------------------------------------------------------------
			glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
			//从单位cube的中心点看向正方体6个面的view矩阵
			glm::mat4 captureViews[] =
			{
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
			};
			OpenGLShader* hdrToCubeMapShader = static_cast<OpenGLShader*>(Renderer::GetShaderLibrary()->Get("HdrToCubeMap").get());




			TextureSpecification envCubemapspec;
			envCubemapspec.Width = 512;
			envCubemapspec.Height = 512;
			envCubemapspec.Format = RHIFormat::RHI_FORMAT_R16G16B16_SFLOAT;
			envCubemapspec.Wrap = RHISamplerAddressMode::RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			envCubemapspec.Minf = RHIFilter::RHI_FILTER_CUBIC_EXT;
			envCubemapspec.Maxf = RHIFilter::RHI_FILTER_LINEAR;

			Ref<TextureCube> envCubemap = TextureCube::Create(envCubemapspec);




			Ref<StaticMesh> cube = MeshFactory::CreatCube(glm::vec3(2.0f));
			framebuffer->Bind();
			OpenGLRenderCommand::SetClearColor(glm::vec4(0.02f, 0.02f, 0.02f, 1.0f));
			OpenGLRenderCommand::Clear();

			hdrToCubeMapShader->Bind();
			hdrToCubeMapShader->UploadUniformInt("equirectangularMap", 1);
			hdrTexture->Bind(1);
			hdrToCubeMapShader->UploadUniformMat4("projection", captureProjection);
			OpenGLRenderCommand::EnableSetStencil(false);
			OpenGLRenderCommand::EnableBlend(false);
			for (unsigned int i = 0; i < 6; ++i)
			{

				hdrToCubeMapShader->UploadUniformMat4("view", captureViews[i]);
				framebuffer->BindTextureToColorAttachMent(envCubemap, i);
				OpenGLRenderCommand::Clear();
				SubmitStaticMesh(cube, Renderer::GetShaderLibrary()->Get("HdrToCubeMap"));

			}
			OpenGLRenderCommand::GetError();
			framebuffer->Unbind();

			envCubemap->GenerateMipmap();
			OpenGLRenderCommand::GetError();

			//---irradiancemap--------------------------------------------------//

			TextureSpecification irradianceCubemapspec;
			irradianceCubemapspec.Width = 32;
			irradianceCubemapspec.Height = 32;
			irradianceCubemapspec.Format = RHIFormat::RHI_FORMAT_R16G16B16_SFLOAT;
			irradianceCubemapspec.Wrap = RHISamplerAddressMode::RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			irradianceCubemapspec.Minf = RHIFilter::RHI_FILTER_CUBIC_EXT;
			irradianceCubemapspec.Maxf = RHIFilter::RHI_FILTER_LINEAR;
			irradianceCubemapspec.GenerateMips = true;
			Ref<TextureCube> irradiancemap = TextureCube::Create(irradianceCubemapspec);
			framebuffer->Resize(32, 32);

			OpenGLShader* genIrradianceShader = static_cast<OpenGLShader*>(Renderer::GetShaderLibrary()->Get("genIrradiance").get());
			genIrradianceShader->Bind();
			genIrradianceShader->UploadUniformInt("environmentMap", 2);
			genIrradianceShader->UploadUniformMat4("projection", captureProjection);
			envCubemap->Bind(2);
			framebuffer->Bind();
			for (unsigned int i = 0; i < 6; ++i)
			{
				genIrradianceShader->UploadUniformMat4("view", captureViews[i]);
				framebuffer->BindTextureToColorAttachMent(irradiancemap, i);
				OpenGLRenderCommand::Clear();
				SubmitStaticMesh(cube, Renderer::GetShaderLibrary()->Get("genIrradiance"));
			}
			OpenGLRenderCommand::GetError();

			m_SkyBox = envCubemap;

			m_Environment.IrradianceMap = irradiancemap;


			//------PreFilterRadianceMap-----------------------------------------//


			TextureSpecification prefilterMapspec;
			prefilterMapspec.Width = 128;
			prefilterMapspec.Height = 128;
			prefilterMapspec.Wrap = RHISamplerAddressMode::RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			prefilterMapspec.Minf = RHIFilter::RHI_FILTER_CUBIC_IMG;
			prefilterMapspec.Maxf = RHIFilter::RHI_FILTER_LINEAR;
			prefilterMapspec.GenerateMips = true;
			prefilterMapspec.Format = RHIFormat::RHI_FORMAT_R16G16B16_SFLOAT;

			Ref<TextureCube> prefilterMap = TextureCube::Create(prefilterMapspec);
			OpenGLRenderCommand::GetError();
			OpenGLShader* prefilterShader = static_cast<OpenGLShader*>(Renderer::GetShaderLibrary()->Get("prefilter").get());
			prefilterShader->Bind();
			prefilterShader->UploadUniformInt("environmentMap", 2);
			prefilterShader->UploadUniformMat4("projection", captureProjection);
			envCubemap->Bind(2);


			uint32_t  maxMipLevels = 5;
			for (unsigned int mip = 0; mip < maxMipLevels; ++mip)
			{
				//调整 ViewPort大小,保证和 mip-level 维持统一
				unsigned int mipWidth = static_cast<unsigned int>(128 * std::pow(0.5, mip));
				unsigned int mipHeight = static_cast<unsigned int>(128 * std::pow(0.5, mip));
				framebuffer->Resize(mipWidth, mipHeight);
				framebuffer->Bind();

				float roughness = (float)mip / (float)(maxMipLevels - 1);
				prefilterShader->UploadUniformFloat("roughness", roughness);
				for (unsigned int i = 0; i < 6; ++i)
				{
					prefilterShader->UploadUniformMat4("view", captureViews[i]);
					framebuffer->BindTextureToColorAttachMent(prefilterMap, i, mip);
					OpenGLRenderCommand::Clear();
					SubmitStaticMesh(cube, Renderer::GetShaderLibrary()->Get("prefilter"));
				}
			}
			framebuffer->Unbind();
			OpenGLRenderCommand::GetError();

			//------PreCompute BRDF-LUT----------------------------------//
			TextureSpecification  brdfLUTspec;
			brdfLUTspec.Width = 512;
			brdfLUTspec.Height = 512;
			brdfLUTspec.Wrap = RHISamplerAddressMode::RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			brdfLUTspec.Minf = RHIFilter::RHI_FILTER_LINEAR;
			brdfLUTspec.Maxf = RHIFilter::RHI_FILTER_LINEAR;
			brdfLUTspec.Format = RHIFormat::RHI_FORMAT_R16G16_SFLOAT;

			Ref<Texture2D> brdfLUTTexture = Texture2D::Create(brdfLUTspec);

			framebuffer->Resize(512, 512);
			framebuffer->Bind();
			framebuffer->BindTextureToColorAttachMent(brdfLUTTexture);

			OpenGLShader* brdfShader = static_cast<OpenGLShader*>(Renderer::GetShaderLibrary()->Get("brdf").get());
			brdfShader->Bind();
			OpenGLRenderCommand::Clear();
			OpenGLRenderCommand::DrawQuad();

			framebuffer->Unbind();


			//----Init the Scene Environment
			m_SkyBox = envCubemap;
			m_BrdfLUT = brdfLUTTexture;
			m_Environment.IrradianceMap = irradiancemap;
			m_Environment.RadianceMap = prefilterMap;

		}
		OpenGLRenderCommand::EnableCullFace(true);
		OpenGLRenderCommand::CullFace(CullFaceOption::BACK);
	}

	void SceneRenderer::BeginScene(SceneInfo info)
	{

		CORE_ASSERT(m_RenderScene, "Render a invalid Scene with nullptr handle");
		m_Active = true;
		m_SceneInfo = info;
		m_FrameBuffer->Bind();
		//renderer
		OpenGLRenderCommand::SetClearColor({ 0.02f, 0.02f, 0.02f, 1.0f });
		//OpenGLRenderCommand::SetClearColor({ 0.8f, 0.8f, 0.8f, 1.0f });
		OpenGLRenderCommand::Clear();
		
	}

	void SceneRenderer::EndScene()
	{
		m_FrameBuffer->Unbind();
		m_Active = false;
	}

	void SceneRenderer::SetScene(RenderScene* scene)
	{
		CORE_ASSERT(scene, "Handel a nullptr scene");
		m_RenderScene = scene;
	}

	void SceneRenderer::SetViewportSize(uint32_t width, uint32_t height)
	{
		m_ViewportWidth = width;
		m_ViewportHeight = height;
		m_FrameBuffer->Resize(width, height);
	}

	

	void SceneRenderer::SubmitStaticMesh(Ref<StaticMesh> mesh, Ref<MaterialTable> materialTabel, glm::mat4 transform)
	{
		auto& VAOs = mesh->GetMeshSource()->GetVertexArray();
		auto shader = static_cast<OpenGLShader*>( Renderer::GetShaderLibrary()->Get("StaticMeshShader").get());

		shader->Bind();

		glm::mat4 viewprojection = m_SceneInfo.sceneCamera.camera.GetProjectionMatrix()*m_SceneInfo.sceneCamera.viewMatrix;
		shader->UploadUniformMat4("U_ViewProjection", viewprojection);
		shader->UploadUniformMat4("U_Transform", transform);

		{
			//SetLight
			{
				//Dir
				shader->UploadUniform3Float("dirLight.LightDir", m_SceneInfo.dirLight.Dirction);
				shader->UploadUniform3Float("dirLight.Ambient_Intensity", m_SceneInfo.dirLight.Ambient_Intensity);
				shader->UploadUniform3Float("dirLight.Diffuse_Intensity", m_SceneInfo.dirLight.Diffuse_Intensity);
				shader->UploadUniform3Float("dirLight.Specular_Intensity", m_SceneInfo.dirLight.Specular_Intensity);

				//P3oint
				shader->UploadUniform3Float("pointLight.Position", m_SceneInfo.pointLight.Position);
				shader->UploadUniform3Float("pointLight.Ambient_Intensity", m_SceneInfo.pointLight.Ambient_Intensity);
				shader->UploadUniform3Float("pointLight.Diffuse_Intensity", m_SceneInfo.pointLight.Diffuse_Intensity);
				shader->UploadUniform3Float("pointLight.Specular_Intensity", m_SceneInfo.pointLight.Specular_Intensity);
			}
			//SetScene
			{
				shader->UploadUniform3Float("U_ViewPos", m_SceneInfo.sceneCamera.Position);
				
			}
		}

		for (auto& mesh : mesh->GetSubMesh())
		{
			auto& VA = VAOs[mesh];
			VA->Bind();
			auto subMtl = materialTabel->GetMaterialAsset(mesh)->GetMaterial();
			subMtl->SetShader(Renderer::GetShaderLibrary()->Get("StaticMeshShader"));
			subMtl->Invalidate();

			OpenGLRenderCommand::DrawIndexed(VA);
			VA->Unbind();
		}
	}

	void SceneRenderer::SubmitStaticMesh(Ref<StaticMesh> mesh, Ref<Shader> shader)
	{
		auto& VAOs = mesh->GetMeshSource()->GetVertexArray();
		auto openglshader = static_cast<OpenGLShader*>(shader.get());
		openglshader->Bind();

		for (auto& mesh : mesh->GetSubMesh())
		{
		
			auto& VA = VAOs[mesh];
			VA->Bind();
			OpenGLRenderCommand::DrawIndexed(VA);
			VA->Unbind();
		}
	}

	void SceneRenderer::SubmitPBRStaticMesh(Ref<StaticMesh> mesh, Ref<MaterialTable> materialTabel, glm::mat4 transform)
	{
		auto& VAOs = mesh->GetMeshSource()->GetVertexArray();
		auto shader = static_cast<OpenGLShader*>(Renderer::GetShaderLibrary()->Get("PBRStaticMesh").get());

		shader->Bind();

		glm::mat4 viewprojection = m_SceneInfo.sceneCamera.camera.GetProjectionMatrix() * m_SceneInfo.sceneCamera.viewMatrix;
		shader->UploadUniformMat4("U_ViewProjection", viewprojection);
		shader->UploadUniformMat4("U_Transform", transform);


		uint32_t maxSlot = 0;
		for (auto& [index, mtl] : materialTabel->GetMaterials())
		{
			maxSlot = glm::max((uint32_t)mtl->GetMaterial()->GetTextures().size(), maxSlot);
		}
		{
			//SetLight
			{
				//Point
				shader->UploadUniform3Float("lightPositions[0]", m_SceneInfo.pointLight.Position);
				shader->UploadUniform3Float("lightPositions[1]", m_SceneInfo.pointLight.Position);
				shader->UploadUniform3Float("lightPositions[2]", m_SceneInfo.pointLight.Position);
				shader->UploadUniform3Float("lightPositions[3]", m_SceneInfo.pointLight.Position);

				shader->UploadUniform3Float("lightColors[0]", m_SceneInfo.pointLight.Ambient_Intensity);
				shader->UploadUniform3Float("lightColors[1]", m_SceneInfo.pointLight.Ambient_Intensity);
				shader->UploadUniform3Float("lightColors[2]", m_SceneInfo.pointLight.Ambient_Intensity);
				shader->UploadUniform3Float("lightColors[3]", m_SceneInfo.pointLight.Ambient_Intensity);
			}
			//SetScene
			{
				shader->UploadUniform3Float("U_ViewPos", m_SceneInfo.sceneCamera.Position);
				
				shader->UploadUniformInt(MaterialAsset::GetIrradianceMapLocation(), maxSlot+1);
				m_Environment.IrradianceMap->Bind(maxSlot + 1);
				shader->UploadUniformInt(MaterialAsset::GetPrefilterMapLocation(), maxSlot + 2);
				m_Environment.RadianceMap->Bind(maxSlot + 2);
				shader->UploadUniformInt(MaterialAsset::GetBrdfLUTMapLocation(), maxSlot + 3);
				m_BrdfLUT->Bind(maxSlot + 3);
			}
		}

		for (auto& mesh : mesh->GetSubMesh())
		{
			auto& VA = VAOs[mesh];
			VA->Bind();
			auto subMtl = materialTabel->GetMaterialAsset(mesh)->GetMaterial();
			subMtl->SetShader(Renderer::GetShaderLibrary()->Get("PBRStaticMesh"));
			subMtl->Invalidate();
			shader->Bind();
			
			OpenGLRenderCommand::DrawIndexed(VA);

			VA->Unbind();
		}
	}

	void SceneRenderer::SubmitStaticMeshStencil(Ref<StaticMesh> mesh, glm::mat4 transform)
	{
		auto& VAOs = mesh->GetMeshSource()->GetVertexArray();
		
		auto shader = static_cast<OpenGLShader*>(Renderer::GetShaderLibrary()->Get("StencilShader").get());
		shader->Bind();
		glm::mat4 viewprojection = m_SceneInfo.sceneCamera.camera.GetProjectionMatrix() * m_SceneInfo.sceneCamera.viewMatrix;
		shader->UploadUniformMat4("U_ViewProjection", viewprojection);
		shader->UploadUniformMat4("U_Transform", transform);

		for (auto& mesh : mesh->GetSubMesh())
		{
			auto VA = VAOs[mesh];
			VA->Bind();
			OpenGLRenderCommand::DrawIndexed(VA);
		}
	}

	void SceneRenderer::SubmitStaticMeshPostEffect(Ref<StaticMesh> mesh, Ref<Texture2D> attachment, glm::mat4 transform)
	{
		auto& VAOs = mesh->GetMeshSource()->GetVertexArray();
		auto shader = static_cast<OpenGLShader*>(Renderer::GetShaderLibrary()->Get("PostShader").get());
		shader->Bind();
		glm::mat4 viewprojection = m_SceneInfo.sceneCamera.camera.GetProjectionMatrix() * m_SceneInfo.sceneCamera.viewMatrix;
		shader->UploadUniformMat4("U_ViewProjection", viewprojection);
		shader->UploadUniformMat4("U_Transform", transform);
		attachment->Bind();
		for (auto& mesh : mesh->GetSubMesh())
		{
			auto& VA = VAOs[mesh];
			VA->Bind();
			OpenGLRenderCommand::DrawIndexed(VA);
		}
	}

	void SceneRenderer::SubmitStaticMeshOutLine(Ref<StaticMesh> mesh, glm::mat4 transform)
	{
		auto& VAOs = mesh->GetMeshSource()->GetVertexArray();
		auto shader = static_cast<OpenGLShader*>(Renderer::GetShaderLibrary()->Get("OutLineShader").get());
		shader->Bind();
		glm::mat4 viewprojection = m_SceneInfo.sceneCamera.camera.GetProjectionMatrix() * m_SceneInfo.sceneCamera.viewMatrix;
		shader->UploadUniformMat4("U_ViewProjection", viewprojection);
		shader->UploadUniformMat4("U_Transform", transform);
		auto& material = mesh->GetMaterialTable();

		for (auto& mesh : mesh->GetSubMesh())
		{
			auto subMtl = material->GetMaterialAsset(mesh)->GetMaterial();
			auto& VA = VAOs[mesh];
			subMtl->SetShader(Renderer::GetShaderLibrary()->Get("OutLineShader"));
			subMtl->Invalidate();
			VA->Bind();
			OpenGLRenderCommand::DrawIndexed(VA);
		}
	}

	void SceneRenderer::SubmitStaticMeshToneshading(Ref<StaticMesh> mesh, glm::mat4 transform)
	{
		auto& VAOs = mesh->GetMeshSource()->GetVertexArray();
		auto shader = static_cast<OpenGLShader*>(Renderer::GetShaderLibrary()->Get("ToneShader").get());
		auto& material = mesh->GetMaterialTable();
		shader->Bind();

		glm::mat4 viewprojection = m_SceneInfo.sceneCamera.camera.GetProjectionMatrix() * m_SceneInfo.sceneCamera.viewMatrix;
		shader->UploadUniformMat4("U_ViewProjection", viewprojection);
		shader->UploadUniformMat4("U_Transform", transform);


		{
			//SetLight
			{
				//Dir
				shader->UploadUniform3Float("dirLight.LightDir", m_SceneInfo.dirLight.Dirction);
				shader->UploadUniform3Float("dirLight.Ambient_Intensity", m_SceneInfo.dirLight.Ambient_Intensity);
				shader->UploadUniform3Float("dirLight.Diffuse_Intensity", m_SceneInfo.dirLight.Diffuse_Intensity);
				shader->UploadUniform3Float("dirLight.Specular_Intensity", m_SceneInfo.dirLight.Specular_Intensity);

				//P3oint
				shader->UploadUniform3Float("pointLight.Position", m_SceneInfo.pointLight.Position);
				shader->UploadUniform3Float("pointLight.Ambient_Intensity", m_SceneInfo.pointLight.Ambient_Intensity);
				shader->UploadUniform3Float("pointLight.Diffuse_Intensity", m_SceneInfo.pointLight.Diffuse_Intensity);
				shader->UploadUniform3Float("pointLight.Specular_Intensity", m_SceneInfo.pointLight.Specular_Intensity);
			}
			//SetScene
			{
				shader->UploadUniform3Float("U_ViewPos", m_SceneInfo.sceneCamera.Position);

			}
		}
		for (auto& mesh : mesh->GetSubMesh())
		{
			auto subMtl = material->GetMaterialAsset(mesh)->GetMaterial();
			auto& VA = VAOs[mesh];
			VA->Bind();
			subMtl->SetShader(Renderer::GetShaderLibrary()->Get("ToneShader"));
			subMtl->Invalidate();
			OpenGLRenderCommand::DrawIndexed(VA);
			VA->Unbind();
		}
	}

	void SceneRenderer::SubmitSpotCloud(Ref<StaticMesh> mesh, Ref<MaterialTable> material,glm::mat4 transform)
	{
		auto& VAOs = mesh->GetMeshSource()->GetVertexArray();
		auto shader = static_cast<OpenGLShader*>(mesh->GetMeshSource()->GetMeshShader().get());
		shader->Bind();

		glm::mat4 viewprojection = m_SceneInfo.sceneCamera.camera.GetProjectionMatrix() * m_SceneInfo.sceneCamera.viewMatrix;
		shader->UploadUniformMat4("U_ViewProjection", viewprojection);
		shader->UploadUniformMat4("U_Transform", transform);
		for (auto& mesh : mesh->GetSubMesh())
		{
			auto& VA = VAOs[mesh];
			auto subMtl = material->GetMaterialAsset(mesh)->GetMaterial();
			subMtl->Invalidate();
			
			VA->Bind();
			OpenGLRenderCommand::DrawArray(VA,VA->GetIndexBuffer()->GetCount()/3);
		}
	}

	void SceneRenderer::SubmitMeshEntity(Ref<StaticMesh> mesh, UUID uuid, glm::mat4 transform)
	{
		
	}

	void SceneRenderer::SubmitGrid(float size, float scale, glm::mat4 transform)
	{
		auto shader = static_cast<OpenGLShader*>(Renderer::GetShaderLibrary()->Get("GridShader").get());
		shader->Bind();

		glm::mat4 viewprojection = m_SceneInfo.sceneCamera.camera.GetProjectionMatrix() * m_SceneInfo.sceneCamera.viewMatrix;
		shader->UploadUniformMat4("U_ViewProjection", viewprojection);
		shader->UploadUniformMat4("U_Transform", transform);

		shader->UploadUniformFloat("U_Settings.Scale", scale);
		shader->UploadUniformFloat("U_Settings.Size", size);
		auto& VA = Renderer::GetQuad();
		VA->Bind();
		OpenGLRenderCommand::DrawIndexed(VA);
		VA->Unbind();
	}

	void SceneRenderer::SubmitSkybox(Ref<TextureCube> skybox)
	{
		Ref<StaticMesh> cube = MeshFactory::GetCube();
		auto shader = static_cast<OpenGLShader*>(Renderer::GetShaderLibrary()->Get("skybox").get());
		shader->Bind();
		shader->UploadUniformMat4("U_Projection", m_SceneInfo.sceneCamera.camera.GetProjectionMatrix());
		shader->UploadUniformMat4("U_View", m_SceneInfo.sceneCamera.viewMatrix);
		shader->UploadUniformInt("environmentMap", 1);
		skybox->Bind(1);
		auto& VAOs = cube->GetMeshSource()->GetVertexArray();
		for (auto& mesh : cube->GetSubMesh())
		{
			auto& VA = VAOs[mesh];
			VA->Bind();
			OpenGLRenderCommand::DrawIndexed(VA);
			VA->Unbind();
		}
	}

	
	void SceneRenderer::SubmitStaticMeshDebugNormal(Ref<StaticMesh> mesh, glm::mat4 transform)
	{
		auto shader = static_cast<OpenGLShader*>(Renderer::GetShaderLibrary()->Get("DebugNormalShader").get());
		auto& VAOs = mesh->GetMeshSource()->GetVertexArray();
		shader->Bind();
		glm::mat4 viewprojection = m_SceneInfo.sceneCamera.camera.GetProjectionMatrix() * m_SceneInfo.sceneCamera.viewMatrix;
		shader->UploadUniformMat4("U_ViewProjection", viewprojection);
		shader->UploadUniformMat4("U_Transform", transform);

		auto& material = mesh->GetMaterialTable();

		for (auto& mesh : mesh->GetSubMesh())
		{
			auto subMtl = material->GetMaterialAsset(mesh)->GetMaterial();
			auto& VA = VAOs[mesh];
			VA->Bind();
			//subMtl->SetShader(shader);
			subMtl->SetShader(Renderer::GetShaderLibrary()->Get("DebugNormalShader"));
			subMtl->Invalidate();
			OpenGLRenderCommand::DrawIndexed(VA);
		}

	}

	void SceneRenderer::SubmitStaticMeshDebug(Ref<StaticMesh> mesh, glm::mat4 transform)
	{
		auto& VAOs = mesh->GetMeshSource()->GetVertexArray();
		auto shader = static_cast<OpenGLShader*>(Renderer::GetShaderLibrary()->Get("DebugShader").get());
		auto& material = mesh->GetMaterialTable();
		shader->Bind();

		glm::mat4 viewprojection = m_SceneInfo.sceneCamera.camera.GetProjectionMatrix() * m_SceneInfo.sceneCamera.viewMatrix;
		shader->UploadUniformMat4("U_ViewProjection", viewprojection);
		shader->UploadUniformMat4("U_Transform", transform);

		
		{
			//SetLight
			{
				//Dir
				shader->UploadUniform3Float("dirLight.LightDir", m_SceneInfo.dirLight.Dirction);
				shader->UploadUniform3Float("dirLight.Ambient_Intensity", m_SceneInfo.dirLight.Ambient_Intensity);
				shader->UploadUniform3Float("dirLight.Diffuse_Intensity", m_SceneInfo.dirLight.Diffuse_Intensity);
				shader->UploadUniform3Float("dirLight.Specular_Intensity", m_SceneInfo.dirLight.Specular_Intensity);
						
				//Point	
				shader->UploadUniform3Float("pointLight.Position", m_SceneInfo.pointLight.Position);
				shader->UploadUniform3Float("pointLight.Ambient_Intensity", m_SceneInfo.pointLight.Ambient_Intensity);
				shader->UploadUniform3Float("pointLight.Diffuse_Intensity", m_SceneInfo.pointLight.Diffuse_Intensity);
				shader->UploadUniform3Float("pointLight.Specular_Intensity", m_SceneInfo.pointLight.Specular_Intensity);
			}
			//SetScene
			{
				shader->UploadUniform3Float("U_ViewPos", m_SceneInfo.sceneCamera.Position);
			}
		}
		for (auto& mesh : mesh->GetSubMesh())
		{
			auto subMtl = material->GetMaterialAsset(mesh)->GetMaterial();
			auto& VA = VAOs[mesh];
			VA->Bind();
			subMtl->SetShader(Renderer::GetShaderLibrary()->Get("DebugShader"));
			subMtl->Invalidate();
			OpenGLRenderCommand::DrawIndexed(VA);
		}
	}

	void SceneRenderer::SubmitToneCharactorShader(Ref<StaticMesh> mesh, glm::mat4 transform)
	{
		auto& VAOs = mesh->GetMeshSource()->GetVertexArray();
		
		auto shader = static_cast<OpenGLShader*>(Renderer::GetShaderLibrary()->Get("ToneCharactorShader").get());

		auto& material = mesh->GetMaterialTable();
		
		
		shader->Bind();

		glm::mat4 viewprojection = m_SceneInfo.sceneCamera.camera.GetProjectionMatrix() * m_SceneInfo.sceneCamera.viewMatrix;
		shader->UploadUniformMat4("U_ViewProjection", viewprojection);
		shader->UploadUniformMat4("U_Transform", transform);


		{
			//SetLight
			{
				//Dir
				shader->UploadUniform3Float("dirLight.LightDir", m_SceneInfo.dirLight.Dirction);
				shader->UploadUniform3Float("dirLight.Ambient_Intensity", m_SceneInfo.dirLight.Ambient_Intensity);
				shader->UploadUniform3Float("dirLight.Diffuse_Intensity", m_SceneInfo.dirLight.Diffuse_Intensity);
				shader->UploadUniform3Float("dirLight.Specular_Intensity", m_SceneInfo.dirLight.Specular_Intensity);
						
				//Point	
				shader->UploadUniform3Float("pointLight.Position", m_SceneInfo.pointLight.Position);
				shader->UploadUniform3Float("pointLight.Ambient_Intensity", m_SceneInfo.pointLight.Ambient_Intensity);
				shader->UploadUniform3Float("pointLight.Diffuse_Intensity", m_SceneInfo.pointLight.Diffuse_Intensity);
				shader->UploadUniform3Float("pointLight.Specular_Intensity", m_SceneInfo.pointLight.Specular_Intensity);
			}			
			//SetScene	
			{			
				shader->UploadUniform3Float("U_ViewPos", m_SceneInfo.sceneCamera.Position);
			}
		}
		for (auto& mesh : mesh->GetSubMesh())
		{
			auto subMtl = material->GetMaterialAsset(mesh)->GetMaterial();
			auto& VA = VAOs[mesh];
			VA->Bind();
			//TODO: DrawCommand应该和Shader绑定，不和默认材质绑定
			//subMtl->SetShader(shader);
			//TODO:材质系统需要重写，渲染使用可提供材质用来渲染mesh和使用默认材质渲染mesh,材质和模型脱离绑定关系
			shader->Bind();
			subMtl->SetShader(Renderer::GetShaderLibrary()->Get("ToneCharactorShader"));
			subMtl->Invalidate();
			OpenGLRenderCommand::DrawIndexed(VA);
			VA->Unbind();
		}
	}



}