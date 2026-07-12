#include "EnvironmentImporter.h"

namespace Kans {
  void Importe(const Ref<AssetMetaData>& metadata, Ref<Asset>& asset)
  {

		throw std::logic_error("The method or operation is not implemented.");
		/*
		
		//prepareSceneEnv
		OpenGLRenderCommand::EnableCullFace(false);
		OpenGLRenderCommand::Clear();
		Environment environment;

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

			// ????cubemap6?????viewmatrix
			// ----------------------------------------------------------------------------------------------
			glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
			//???��cube???????????????6?????view????
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
			
			framebuffer->Unbind();

			envCubemap->GenerateMipmap();
			OpenGLRenderCommand::GetError();

			//---irradianceMap--------------------------------------------------//

			TextureSpecification irradianceCubemapspec;
			irradianceCubemapspec.Width = 512;
			irradianceCubemapspec.Height = 512;
			irradianceCubemapspec.Format = RHIFormat::RHI_FORMAT_R16G16B16_SFLOAT;
			irradianceCubemapspec.Wrap = RHISamplerAddressMode::RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			irradianceCubemapspec.Minf = RHIFilter::RHI_FILTER_CUBIC_EXT;
			irradianceCubemapspec.Maxf = RHIFilter::RHI_FILTER_LINEAR;
			irradianceCubemapspec.GenerateMips = true;
			Ref<TextureCube> irradianceMap = TextureCube::Create(irradianceCubemapspec);
			framebuffer->Resize(512, 512);

			OpenGLShader* genIrradianceShader = static_cast<OpenGLShader*>(Renderer::GetShaderLibrary()->Get("genIrradiance").get());
			genIrradianceShader->Bind();
			genIrradianceShader->UploadUniformInt("environmentMap", 2);
			genIrradianceShader->UploadUniformMat4("projection", captureProjection);
			envCubemap->Bind(2);
			framebuffer->Bind();
			for (unsigned int i = 0; i < 6; ++i)
			{
				genIrradianceShader->UploadUniformMat4("view", captureViews[i]);
				framebuffer->BindTextureToColorAttachMent(irradianceMap, i);
				OpenGLRenderCommand::Clear();
				SubmitStaticMesh(cube, Renderer::GetShaderLibrary()->Get("genIrradiance"));
			}
			OpenGLRenderCommand::GetError();

			m_SkyBox = envCubemap;

			environment.IrradianceMap = irradianceMap;


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
				//???? ViewPort??��,????? mip-level ?????
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
			environment.IrradianceMap = irradianceMap;
			environment.RadianceMap = prefilterMap;
			//now the cube map is cahed in to memory
			//but how to cache the memory in to disk 

			environment.IrradianceMap = Texture2DImport(irradianceMap) ;
			environment.RadianceMap = Texture2DImport(irradianceMap);

		}
		OpenGLRenderCommand::EnableCullFace(true);
		OpenGLRenderCommand::CullFace(CullFaceOption::BACK);
		
		*/
  }
	bool TryLoadData(const Ref<AssetMetaData>& metadata, Ref<Asset>& asset)
  {
		throw std::logic_error("The method or operation is not implemented.");
  }
}