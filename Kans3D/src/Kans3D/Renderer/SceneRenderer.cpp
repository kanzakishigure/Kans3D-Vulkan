#include "kspch.h"
#include "Kans3D/Renderer/SceneRenderer.h"
#include "Kans3D/Core/Log/Log.h"
#include "Kans3D/Renderer/Renderer.h"
// test
#include "imgui.h"
// temp
#include <istream>
#include <ostream>
#include <string>
#include "Kans3D/Core/Application.h"
#include "Kans3D/Platform/OpenGL/OpenGLShader.h"
#include "Kans3D/Renderer/Resource/MeshFactory.h"
#define EnableEnv true

namespace Kans
{

    SceneRenderer::SceneRenderer(RenderScene* scene, SceneRendererSpecification spec) :
        m_RenderScene(scene), m_Specification(spec)
    {

        Init();
    }

    SceneRenderer::~SceneRenderer() = default;

    void SceneRenderer::DrawGeometry(const Ref<VertexArray>& geometry)
    {
        geometry->Bind();
        OpenGLRenderCommand::DrawIndexed(geometry);
        geometry->Unbind();
    }

    void SceneRenderer::SetAssetManager(AssetManager* assets)
    {
        m_AssetCache = assets ? CreateScope<RenderAssetCache>(*assets) : nullptr;
    }

    void SceneRenderer::SubmitModel(const ProductModelComponent& model, const glm::mat4& transform)
    {
        if (!m_AssetCache)
            return;
        const ModelInstances* instances = m_AssetCache->GetModel(model.Model);
        if (!instances)
            return;
        auto  shaderRef = Renderer::GetShaderLibrary()->Get("ProductPBR");
        auto* shader    = static_cast<OpenGLShader*>(shaderRef.get());
        shader->Bind();
        shader->UploadUniformMat4("U_ViewProjection",
                                  m_SceneInfo.sceneCamera.camera.GetProjectionMatrix() *
                                      m_SceneInfo.sceneCamera.viewMatrix);
        shader->UploadUniform3Float("U_ViewPos", m_SceneInfo.sceneCamera.Position);
        for (int i = 0; i < 4; ++i)
        {
            shader->UploadUniform3Float("lightPositions[" + std::to_string(i) + "]", m_SceneInfo.pointLight.Position);
            shader->UploadUniform3Float("lightColors[" + std::to_string(i) + "]",
                                        m_SceneInfo.pointLight.Ambient_Intensity);
        }
        // Texture units: 0-4 material, 5-7 environment.
        shader->UploadUniformInt("U_AlbedoTexture", 0);
        shader->UploadUniformInt("U_NormalTexture", 1);
        shader->UploadUniformInt("U_MetallicRoughnessTexture", 2);
        shader->UploadUniformInt("U_OcclusionTexture", 3);
        shader->UploadUniformInt("U_EmissiveTexture", 4);
        shader->UploadUniformInt("U_IrradianceMap", 5);
        shader->UploadUniformInt("U_PrefilterMap", 6);
        shader->UploadUniformInt("U_BrdfLUTMap", 7);
        if (m_Environment.IrradianceMap)
            m_Environment.IrradianceMap->Bind(5);
        if (m_Environment.RadianceMap)
            m_Environment.RadianceMap->Bind(6);
        if (m_BrdfLUT)
            m_BrdfLUT->Bind(7);

        const auto white       = Renderer::GetWhiteTexture();
        const auto bindOrWhite = [&](AssetID id, uint32_t slot) {
            auto texture = m_AssetCache->GetTexture(id);
            (texture ? texture : white)->Bind(slot);
            return static_cast<bool>(texture);
        };
        const GpuMaterial       defaultMaterial;
        std::vector<ShaderDraw> shaderDraws;
        for (const auto& instance : instances->Instances)
        {
            const GpuMesh* mesh = m_AssetCache->GetMesh(instance.Mesh);
            if (!mesh)
                continue;
            const AssetID      materialID = model.ResolveMaterial(instance.Mesh, mesh->Material);
            const GpuMaterial* material   = materialID.IsValid() ? m_AssetCache->GetMaterial(materialID) : nullptr;
            // Shader materials draw with their own program; until it is ready (or if
            // it fails) the part is shaded with the default material.
            if (material && material->Shader.IsValid())
            {
                if (m_AssetCache->GetMaterialBindings(materialID))
                {
                    shaderDraws.push_back({mesh, transform * instance.Transform, materialID});
                    continue;
                }
                material = nullptr;
            }
            if (!material)
                material = &defaultMaterial;
            shader->UploadUniformMat4("U_Transform", transform * instance.Transform);
            shader->UploadUniform4Float("U_BaseColor", material->BaseColor);
            shader->UploadUniformFloat("U_Metallic", material->Metallic);
            shader->UploadUniformFloat("U_Roughness", material->Roughness);
            bindOrWhite(material->AlbedoTexture, 0);
            shader->UploadUniformInt("U_HasNormalTexture", bindOrWhite(material->NormalTexture, 1));
            bindOrWhite(material->MetallicRoughnessTexture, 2);
            bindOrWhite(material->OcclusionTexture, 3);
            shader->UploadUniformInt("U_HasEmissiveTexture", bindOrWhite(material->EmissiveTexture, 4));
            mesh->VertexArray->Bind();
            OpenGLRenderCommand::DrawIndexed(mesh->VertexArray);
        }
        if (!shaderDraws.empty())
            DrawShaderMaterials(shaderDraws);
    }

    void SceneRenderer::DrawShaderMaterials(const std::vector<ShaderDraw>& draws)
    {
        EngineFrameUniforms frame;
        frame.ViewProjection =
            m_SceneInfo.sceneCamera.camera.GetProjectionMatrix() * m_SceneInfo.sceneCamera.viewMatrix;
        frame.CameraPosition = glm::vec4(m_SceneInfo.sceneCamera.Position, 1.0f);
        for (uint32_t i = 0; i < EngineLightCount; ++i)
        {
            frame.LightPositions[i] = glm::vec4(m_SceneInfo.pointLight.Position, 1.0f);
            frame.LightColors[i]    = glm::vec4(m_SceneInfo.pointLight.Ambient_Intensity, 1.0f);
            frame.LightDiffuse[i]   = glm::vec4(m_SceneInfo.pointLight.Diffuse_Intensity, 1.0f);
        }
        frame.Time = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_StartTime).count();
        if (!m_FrameUniforms)
        {
            m_FrameUniforms  = CreateScope<GpuUniformBuffer>(sizeof(EngineFrameUniforms));
            m_ObjectUniforms = CreateScope<GpuUniformBuffer>(sizeof(EngineObjectUniforms));
        }
        m_FrameUniforms->Update(&frame, sizeof(frame));
        m_FrameUniforms->Bind(GpuShader::FrameBlockPoint);
        m_ObjectUniforms->Bind(GpuShader::ObjectBlockPoint);
        if (m_Environment.IrradianceMap)
            m_Environment.IrradianceMap->Bind(GpuShader::IrradianceUnit);
        if (m_Environment.RadianceMap)
            m_Environment.RadianceMap->Bind(GpuShader::PrefilterUnit);
        if (m_BrdfLUT)
            m_BrdfLUT->Bind(GpuShader::BrdfLUTUnit);

        const auto white = Renderer::GetWhiteTexture();
        for (const auto& draw : draws)
        {
            const GpuMaterial*         material = m_AssetCache->GetMaterial(draw.Material);
            const GpuShader*           shader   = m_AssetCache->GetShader(material->Shader);
            const GpuMaterialBindings* bindings = m_AssetCache->GetMaterialBindings(draw.Material);
            shader->Bind();
            EngineObjectUniforms object;
            object.Transform    = draw.Transform;
            object.NormalMatrix = glm::transpose(glm::inverse(draw.Transform));
            m_ObjectUniforms->Update(&object, sizeof(object));
            for (const auto& [binding, buffer] : bindings->Blocks)
                buffer.Bind(shader->MaterialBlockPoints().at(binding));
            for (const auto& [binding, unit] : shader->MaterialSamplerUnits())
            {
                Ref<Texture2D> texture;
                for (const auto& [textureBinding, id] : bindings->Textures)
                    if (textureBinding == binding)
                        texture = m_AssetCache->GetTexture(id);
                (texture ? texture : white)->Bind(unit);
            }
            draw.Mesh->VertexArray->Bind();
            OpenGLRenderCommand::DrawIndexed(draw.Mesh->VertexArray);
        }
    }

    void SceneRenderer::SubmitModelGeometry(const ProductModelComponent& model,
                                            const glm::mat4&             transform,
                                            const std::string&           shaderName)
    {
        if (!m_AssetCache)
            return;
        const ModelInstances* instances = m_AssetCache->GetModel(model.Model);
        if (!instances)
            return;
        auto  shaderRef = Renderer::GetShaderLibrary()->Get(shaderName);
        auto* shader    = static_cast<OpenGLShader*>(shaderRef.get());
        shader->Bind();
        shader->UploadUniformMat4("U_ViewProjection",
                                  m_SceneInfo.sceneCamera.camera.GetProjectionMatrix() *
                                      m_SceneInfo.sceneCamera.viewMatrix);
        shader->UploadUniform3Float("U_ViewPos", m_SceneInfo.sceneCamera.Position);
        for (const auto& instance : instances->Instances)
            if (const GpuMesh* mesh = m_AssetCache->GetMesh(instance.Mesh))
            {
                shader->UploadUniformMat4("U_Transform", transform * instance.Transform);
                mesh->VertexArray->Bind();
                OpenGLRenderCommand::DrawIndexed(mesh->VertexArray);
            }
    }

    void SceneRenderer::Init()
    {
        m_ViewportWidth  = m_RenderScene->m_ViewportWidth;
        m_ViewportHeight = m_RenderScene->m_ViewportHeight;
        // FrameBuffer init
        {

            FrameBufferSpecification spec;
            spec.Width           = m_ViewportWidth;
            spec.Height          = m_ViewportHeight;
            spec.SwapChainTarget = true;
            // Color                           normal													Depth
            spec.AttachmentSpecification = {
                FrameBufferTextureFormat::RGBA8, FrameBufferTextureFormat::RGBA8, FrameBufferTextureFormat::Depth};
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
        OpenGLRenderCommand::GetError();
        // prepareSceneEnv
        OpenGLRenderCommand::EnableCullFace(false);
        OpenGLRenderCommand::Clear();

        {
            FrameBufferSpecification frameBufferSpec;
            frameBufferSpec.Width                   = 512;
            frameBufferSpec.Height                  = 512;
            frameBufferSpec.SwapChainTarget         = true;
            frameBufferSpec.AttachmentSpecification = {FrameBufferTextureFormat::RGB16F,
                                                       FrameBufferTextureFormat::Depth};

            Ref<FrameBuffer> framebuffer = FrameBuffer::Create(frameBufferSpec);

            TextureSpecification textureSepc;
            textureSepc.Wrap = RHISamplerAddressMode::RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            textureSepc.Minf = RHIFilter::RHI_FILTER_LINEAR;
            textureSepc.Maxf = RHIFilter::RHI_FILTER_LINEAR;

            Ref<Texture2D> hdrTexture = Texture2D::Create(textureSepc, m_RenderScene->m_EnvironmentPath);

            // 设置cubemap6个面的viewmatrix
            // ----------------------------------------------------------------------------------------------
            glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
            // 从单位cube的中心点看向正方体6个面的view矩阵
            glm::mat4 captureViews[] = {
                glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
                glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
                glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
                glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f)),
                glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
                glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, -1.0f, 0.0f))};
            OpenGLShader* hdrToCubeMapShader =
                static_cast<OpenGLShader*>(Renderer::GetShaderLibrary()->Get("HdrToCubeMap").get());

            TextureSpecification envCubemapspec;
            envCubemapspec.Width  = 512;
            envCubemapspec.Height = 512;
            envCubemapspec.Format = RHIFormat::RHI_FORMAT_R16G16B16_SFLOAT;
            envCubemapspec.Wrap   = RHISamplerAddressMode::RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            envCubemapspec.Minf   = RHIFilter::RHI_FILTER_CUBIC_EXT;
            envCubemapspec.Maxf   = RHIFilter::RHI_FILTER_LINEAR;

            Ref<TextureCube> envCubemap = TextureCube::Create(envCubemapspec);

            const auto cube = MeshFactory::GetCube();
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
                DrawGeometry(cube);
            }

            framebuffer->Unbind();

            envCubemap->GenerateMipmap();
            OpenGLRenderCommand::GetError();

            //---irradiancemap--------------------------------------------------//

            TextureSpecification irradianceCubemapspec;
            irradianceCubemapspec.Width        = 512;
            irradianceCubemapspec.Height       = 512;
            irradianceCubemapspec.Format       = RHIFormat::RHI_FORMAT_R16G16B16_SFLOAT;
            irradianceCubemapspec.Wrap         = RHISamplerAddressMode::RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            irradianceCubemapspec.Minf         = RHIFilter::RHI_FILTER_CUBIC_EXT;
            irradianceCubemapspec.Maxf         = RHIFilter::RHI_FILTER_LINEAR;
            irradianceCubemapspec.GenerateMips = true;
            Ref<TextureCube> irradiancemap     = TextureCube::Create(irradianceCubemapspec);
            framebuffer->Resize(512, 512);

            OpenGLShader* genIrradianceShader =
                static_cast<OpenGLShader*>(Renderer::GetShaderLibrary()->Get("genIrradiance").get());
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
                DrawGeometry(cube);
            }
            OpenGLRenderCommand::GetError();

            m_SkyBox = envCubemap;

            m_Environment.IrradianceMap = irradiancemap;

            //------PreFilterRadianceMap-----------------------------------------//

            TextureSpecification prefilterMapspec;
            prefilterMapspec.Width        = 128;
            prefilterMapspec.Height       = 128;
            prefilterMapspec.Wrap         = RHISamplerAddressMode::RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            prefilterMapspec.Minf         = RHIFilter::RHI_FILTER_CUBIC_IMG;
            prefilterMapspec.Maxf         = RHIFilter::RHI_FILTER_LINEAR;
            prefilterMapspec.GenerateMips = true;
            prefilterMapspec.Format       = RHIFormat::RHI_FORMAT_R16G16B16_SFLOAT;

            Ref<TextureCube> prefilterMap = TextureCube::Create(prefilterMapspec);
            OpenGLRenderCommand::GetError();
            OpenGLShader* prefilterShader =
                static_cast<OpenGLShader*>(Renderer::GetShaderLibrary()->Get("prefilter").get());
            prefilterShader->Bind();
            prefilterShader->UploadUniformInt("environmentMap", 2);
            prefilterShader->UploadUniformMat4("projection", captureProjection);
            envCubemap->Bind(2);

            uint32_t maxMipLevels = 5;
            for (unsigned int mip = 0; mip < maxMipLevels; ++mip)
            {
                // 调整 ViewPort大小,保证和 mip-level 维持统一
                unsigned int mipWidth  = static_cast<unsigned int>(128 * std::pow(0.5, mip));
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
                    DrawGeometry(cube);
                }
            }
            framebuffer->Unbind();
            OpenGLRenderCommand::GetError();

            //------PreCompute BRDF-LUT----------------------------------//
            TextureSpecification brdfLUTspec;
            brdfLUTspec.Width  = 512;
            brdfLUTspec.Height = 512;
            brdfLUTspec.Wrap   = RHISamplerAddressMode::RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            brdfLUTspec.Minf   = RHIFilter::RHI_FILTER_LINEAR;
            brdfLUTspec.Maxf   = RHIFilter::RHI_FILTER_LINEAR;
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
            m_SkyBox                    = envCubemap;
            m_BrdfLUT                   = brdfLUTTexture;
            m_Environment.IrradianceMap = irradiancemap;
            m_Environment.RadianceMap   = prefilterMap;
        }
        OpenGLRenderCommand::EnableCullFace(true);
        OpenGLRenderCommand::CullFace(CullFaceOption::BACK);
    }

    void SceneRenderer::UpdateEnvironment(std::string path)
    {
        m_RenderScene->m_EnvironmentPath = path;
        PrepareEnvironment();
    }

    void SceneRenderer::BeginScene(SceneInfo info)
    {

        CORE_ASSERT(m_RenderScene, "Render a invalid Scene with nullptr handle");
        m_Active    = true;
        m_SceneInfo = info;
        if (m_AssetCache)
            m_AssetCache->BeginFrame();
        m_FrameBuffer->Bind();
        // renderer
        OpenGLRenderCommand::SetClearColor({0.02f, 0.02f, 0.02f, 1.0f});
        // OpenGLRenderCommand::SetClearColor({ 0.8f, 0.8f, 0.8f, 1.0f });
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
        m_ViewportWidth  = width;
        m_ViewportHeight = height;
        m_FrameBuffer->Resize(width, height);
    }

    void SceneRenderer::SubmitGrid(float size, float scale, glm::mat4 transform)
    {
        auto shader = static_cast<OpenGLShader*>(Renderer::GetShaderLibrary()->Get("GridShader").get());
        shader->Bind();

        glm::mat4 viewprojection =
            m_SceneInfo.sceneCamera.camera.GetProjectionMatrix() * m_SceneInfo.sceneCamera.viewMatrix;
        shader->UploadUniformMat4("U_ViewProjection", viewprojection);
        shader->UploadUniformMat4("U_Transform", transform);

        shader->UploadUniformFloat("U_Settings.Scale", scale);
        shader->UploadUniformFloat("U_Settings.Size", size);
        auto VA = Renderer::GetQuad();
        VA->Bind();
        OpenGLRenderCommand::DrawIndexed(VA);
        VA->Unbind();
    }

    void SceneRenderer::SubmitSkybox(Ref<TextureCube> skybox)
    {
        auto shader = static_cast<OpenGLShader*>(Renderer::GetShaderLibrary()->Get("skybox").get());
        shader->Bind();
        shader->UploadUniformMat4("U_Projection", m_SceneInfo.sceneCamera.camera.GetProjectionMatrix());
        shader->UploadUniformMat4("U_View", m_SceneInfo.sceneCamera.viewMatrix);
        shader->UploadUniformInt("environmentMap", 1);
        skybox->Bind(1);
        DrawGeometry(MeshFactory::GetCube());
    }

} // namespace Kans
