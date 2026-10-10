#include "kspch.h"
#include <glad/glad.h>
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
    namespace
    {
        void SetCapability(GLenum capability, GLboolean enabled)
        {
            if (enabled)
                glEnable(capability);
            else
                glDisable(capability);
        }

        void ApplyStencil(ModelStencilMode mode)
        {
            SetCapability(GL_STENCIL_TEST, mode == ModelStencilMode::WriteReference);
            glStencilFunc(GL_ALWAYS, 1, 0xff);
            glStencilMask(mode == ModelStencilMode::WriteReference ? 0xff : 0);
            glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        }

        struct RasterState
        {
            GLboolean Cull = glIsEnabled(GL_CULL_FACE), Blend = glIsEnabled(GL_BLEND),
                      Depth = glIsEnabled(GL_DEPTH_TEST), DepthWrite;
            GLint     CullMode, DepthFunc, Polygon[2], SrcRGB, DstRGB, SrcAlpha, DstAlpha, EquationRGB, EquationAlpha;
            GLboolean ColorWrite[4], Stencil = glIsEnabled(GL_STENCIL_TEST);
            struct StencilFace
            {
                GLint Function, Reference, ValueMask, WriteMask, Fail, DepthFail, DepthPass;
            };
            StencilFace Front {}, Back {};
            RasterState()
            {
                glGetBooleanv(GL_DEPTH_WRITEMASK, &DepthWrite);
                glGetBooleanv(GL_COLOR_WRITEMASK, ColorWrite);
                glGetIntegerv(GL_STENCIL_FUNC, &Front.Function);
                glGetIntegerv(GL_STENCIL_REF, &Front.Reference);
                glGetIntegerv(GL_STENCIL_VALUE_MASK, &Front.ValueMask);
                glGetIntegerv(GL_STENCIL_WRITEMASK, &Front.WriteMask);
                glGetIntegerv(GL_STENCIL_FAIL, &Front.Fail);
                glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, &Front.DepthFail);
                glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, &Front.DepthPass);
                glGetIntegerv(GL_STENCIL_BACK_FUNC, &Back.Function);
                glGetIntegerv(GL_STENCIL_BACK_REF, &Back.Reference);
                glGetIntegerv(GL_STENCIL_BACK_VALUE_MASK, &Back.ValueMask);
                glGetIntegerv(GL_STENCIL_BACK_WRITEMASK, &Back.WriteMask);
                glGetIntegerv(GL_STENCIL_BACK_FAIL, &Back.Fail);
                glGetIntegerv(GL_STENCIL_BACK_PASS_DEPTH_FAIL, &Back.DepthFail);
                glGetIntegerv(GL_STENCIL_BACK_PASS_DEPTH_PASS, &Back.DepthPass);
                glGetIntegerv(GL_CULL_FACE_MODE, &CullMode);
                glGetIntegerv(GL_DEPTH_FUNC, &DepthFunc);
                glGetIntegerv(GL_POLYGON_MODE, Polygon);
                glGetIntegerv(GL_BLEND_SRC_RGB, &SrcRGB);
                glGetIntegerv(GL_BLEND_DST_RGB, &DstRGB);
                glGetIntegerv(GL_BLEND_SRC_ALPHA, &SrcAlpha);
                glGetIntegerv(GL_BLEND_DST_ALPHA, &DstAlpha);
                glGetIntegerv(GL_BLEND_EQUATION_RGB, &EquationRGB);
                glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &EquationAlpha);
            }
            void Apply() const
            {
                SetCapability(GL_CULL_FACE, Cull);
                SetCapability(GL_BLEND, Blend);
                SetCapability(GL_DEPTH_TEST, Depth);
                glCullFace(CullMode);
                glDepthFunc(DepthFunc);
                glDepthMask(DepthWrite);
                glPolygonMode(GL_FRONT_AND_BACK, Polygon[0]);
                glBlendFuncSeparate(SrcRGB, DstRGB, SrcAlpha, DstAlpha);
                glBlendEquationSeparate(EquationRGB, EquationAlpha);
                glColorMask(ColorWrite[0], ColorWrite[1], ColorWrite[2], ColorWrite[3]);
                SetCapability(GL_STENCIL_TEST, Stencil);
                const auto restoreFace = [](GLenum face, const StencilFace& state) {
                    glStencilFuncSeparate(face, state.Function, state.Reference, state.ValueMask);
                    glStencilMaskSeparate(face, state.WriteMask);
                    glStencilOpSeparate(face, state.Fail, state.DepthFail, state.DepthPass);
                };
                restoreFace(GL_FRONT, Front);
                restoreFace(GL_BACK, Back);
            }
        };
    } // namespace

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

    void SceneRenderer::DrawMesh(const GpuMesh& mesh)
    {
        mesh.VertexArray->Bind();
        OpenGLRenderCommand::DrawIndexed(mesh.VertexArray);
        ++m_Stats.DrawCalls;
        m_Stats.Triangles += mesh.IndexCount / 3;
    }

    void SceneRenderer::SetAssetManager(AssetManager* assets)
    {
        m_AssetCache = assets ? CreateScope<RenderAssetCache>(*assets) : nullptr;
    }

    void
    SceneRenderer::SubmitModel(const ProductModelComponent& model, const glm::mat4& transform, ModelStencilMode stencil)
    {
        if (!m_AssetCache)
            return;
        const ModelInstances* instances = m_AssetCache->GetModel(model.Model);
        if (!instances)
            return;
        for (const auto& instance : instances->Instances)
            if (const auto* mesh = m_AssetCache->GetMesh(instance.Mesh))
                m_ModelDraws.push_back({instance.Mesh,
                                        transform * instance.Transform,
                                        model.ResolveMaterial(instance.Mesh, mesh->Material),
                                        m_ViewMode,
                                        stencil});
    }

    void SceneRenderer::DrawShaderMaterials(const std::vector<ShaderDraw>& draws)
    {
        if (!m_AssetCache || draws.empty())
            return;
        struct ReadyDraw
        {
            const ShaderDraw*   Draw;
            MaterialRenderState Render;
            float               DistanceSquared;
        };
        std::vector<ReadyDraw> ready;
        for (const auto& draw : draws)
        {
            const auto* mesh = m_AssetCache->GetMesh(draw.Mesh);
            const auto* bindings =
                m_AssetCache->GetMaterialBindings(draw.Material, draw.ViewMode != SceneViewMode::Lit);
            if (!mesh || !bindings)
                continue;
            const glm::vec3 center = glm::vec3(draw.Transform * glm::vec4(mesh->LocalBounds.Center(), 1));
            const glm::vec3 offset = center - m_SceneInfo.sceneCamera.Position;
            ready.push_back({&draw, bindings->Render, glm::dot(offset, offset)});
        }
        std::stable_sort(ready.begin(), ready.end(), [](const ReadyDraw& a, const ReadyDraw& b) {
            const bool aBlend = a.Render.AlphaMode == MaterialAlphaMode::Blend;
            const bool bBlend = b.Render.AlphaMode == MaterialAlphaMode::Blend;
            if (aBlend != bBlend)
                return !aBlend;
            return aBlend && a.DistanceSquared > b.DistanceSquared;
        });

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glCullFace(GL_BACK);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        // Preserve the scene target's opaque alpha when compositing transparent colour.
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);

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
        std::optional<float> uploadedDebugView;
        m_FrameUniforms->Bind(GpuShader::FrameBlockPoint);
        m_ObjectUniforms->Bind(GpuShader::ObjectBlockPoint);
        if (m_Environment.IrradianceMap)
            m_Environment.IrradianceMap->Bind(GpuShader::IrradianceUnit);
        if (m_Environment.RadianceMap)
            m_Environment.RadianceMap->Bind(GpuShader::PrefilterUnit);
        if (m_BrdfLUT)
            m_BrdfLUT->Bind(GpuShader::BrdfLUTUnit);

        for (const auto& item : ready)
        {
            const auto& draw = *item.Draw;
            // Requests may finish during this pass. Snapshot the state used for sorting,
            // but acquire GPU resources at the draw so a cache replacement cannot dangle a queued pointer.
            const auto* mesh = m_AssetCache->GetMesh(draw.Mesh);
            const auto* bindings =
                m_AssetCache->GetMaterialBindings(draw.Material, draw.ViewMode != SceneViewMode::Lit);
            const auto* shader = bindings ? m_AssetCache->GetShader(bindings->Shader) : nullptr;
            if (!mesh || !bindings || !shader)
                continue;
            ApplyStencil(draw.Stencil);
            const auto& render      = item.Render;
            const bool  transparent = render.AlphaMode == MaterialAlphaMode::Blend;
            if (transparent)
                glEnable(GL_BLEND);
            else
                glDisable(GL_BLEND);
            glDepthMask(transparent ? GL_FALSE : GL_TRUE);
            if (render.DoubleSided)
                glDisable(GL_CULL_FACE);
            else
                glEnable(GL_CULL_FACE);
            glPolygonMode(GL_FRONT_AND_BACK, draw.ViewMode == SceneViewMode::Wireframe ? GL_LINE : GL_FILL);
            frame.DebugView = draw.ViewMode == SceneViewMode::BaseColor ? 1.0f :
                              draw.ViewMode == SceneViewMode::Normals   ? 2.0f :
                                                                          0.0f;
            if (!uploadedDebugView || *uploadedDebugView != frame.DebugView)
            {
                m_FrameUniforms->Update(&frame, sizeof(frame));
                uploadedDebugView = frame.DebugView;
            }
            shader->Bind();
            EngineObjectUniforms object;
            object.Transform      = draw.Transform;
            object.NormalMatrix   = glm::transpose(glm::inverse(draw.Transform));
            object.MaterialRender = {float(render.AlphaMode), render.AlphaCutoff, render.DoubleSided ? 1.0f : 0.0f, 0};
            m_ObjectUniforms->Update(&object, sizeof(object));
            for (const auto& [binding, buffer] : bindings->Blocks)
                buffer.Bind(shader->MaterialBlockPoints().at(binding));
            m_AssetCache->BindMaterialTextures(*shader, *bindings);
            DrawMesh(*mesh);
        }
    }

    void SceneRenderer::SubmitModelGeometry(const ProductModelComponent& model,
                                            const glm::mat4&             transform,
                                            const std::string&           shaderName,
                                            ModelGeometryMode            mode)
    {
        m_GeometryDraws.push_back({model, transform, shaderName, mode});
    }

    void SceneRenderer::DrawModelGeometry(const GeometryDraw& draw)
    {
        if (!m_AssetCache)
            return;
        const ModelInstances* instances = m_AssetCache->GetModel(draw.Model.Model);
        const auto            library =
            m_Specification.GeometryShaders ? m_Specification.GeometryShaders : Renderer::GetShaderLibrary();
        if (!instances || !library || !library->Exists(draw.ShaderName))
            return;
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(draw.Mode == ModelGeometryMode::StencilOutline ? GL_LEQUAL : GL_LESS);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        SetCapability(GL_CULL_FACE, draw.Mode == ModelGeometryMode::BackFaces);
        glCullFace(GL_FRONT);
        SetCapability(GL_STENCIL_TEST, draw.Mode == ModelGeometryMode::StencilOutline);
        glStencilFunc(GL_NOTEQUAL, 1, 0xff);
        glStencilMask(0);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        auto  shaderRef = library->Get(draw.ShaderName);
        auto* shader    = static_cast<OpenGLShader*>(shaderRef.get());
        shader->Bind();
        shader->UploadUniformMat4("U_ViewProjection",
                                  m_SceneInfo.sceneCamera.camera.GetProjectionMatrix() *
                                      m_SceneInfo.sceneCamera.viewMatrix);
        shader->UploadUniform3Float("U_ViewPos", m_SceneInfo.sceneCamera.Position);
        for (const auto& instance : instances->Instances)
            if (const GpuMesh* mesh = m_AssetCache->GetMesh(instance.Mesh))
            {
                shader->UploadUniformMat4("U_Transform", draw.Transform * instance.Transform);
                DrawMesh(*mesh);
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
        m_Stats     = {};
        m_ModelDraws.clear();
        m_GeometryDraws.clear();
        if (m_AssetCache)
            m_AssetCache->BeginFrame();
        m_FrameBuffer->Bind();
        // renderer
        OpenGLRenderCommand::SetClearColor({0.02f, 0.02f, 0.02f, 1.0f});
        // OpenGLRenderCommand::SetClearColor({ 0.8f, 0.8f, 0.8f, 1.0f });
        GLboolean depthWrite, colorWrite[4];
        GLint     stencilFront, stencilBack, stencilClear;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
        glGetBooleanv(GL_COLOR_WRITEMASK, colorWrite);
        glGetIntegerv(GL_STENCIL_WRITEMASK, &stencilFront);
        glGetIntegerv(GL_STENCIL_BACK_WRITEMASK, &stencilBack);
        glGetIntegerv(GL_STENCIL_CLEAR_VALUE, &stencilClear);
        glDepthMask(GL_TRUE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glStencilMask(0xff);
        glClearStencil(0);
        OpenGLRenderCommand::Clear();
        glDepthMask(depthWrite);
        glColorMask(colorWrite[0], colorWrite[1], colorWrite[2], colorWrite[3]);
        glStencilMaskSeparate(GL_FRONT, stencilFront);
        glStencilMaskSeparate(GL_BACK, stencilBack);
        glClearStencil(stencilClear);
    }

    void SceneRenderer::AppendModelDraws(const ProductModelComponent& model,
                                         const glm::mat4&             transform,
                                         uint32_t                     id,
                                         std::vector<MeshDraw>&       draws)
    {
        if (!m_AssetCache)
            return;
        if (const ModelInstances* instances = m_AssetCache->GetModel(model.Model))
            for (const auto& instance : instances->Instances)
                if (const GpuMesh* mesh = m_AssetCache->GetMesh(instance.Mesh))
                    draws.push_back({mesh, transform * instance.Transform, id});
    }

    uint32_t SceneRenderer::Pick(const std::vector<MeshDraw>& draws, const glm::mat4& viewProjection, glm::uvec2 pixel)
    {
        return m_Picker.Pick(draws, viewProjection, {m_ViewportWidth, m_ViewportHeight}, pixel);
    }

    void SceneRenderer::EndScene()
    {
        const glm::mat4 viewProjection =
            m_SceneInfo.sceneCamera.camera.GetProjectionMatrix() * m_SceneInfo.sceneCamera.viewMatrix;
        std::vector<MeshDraw> previewDraws;
        if (m_Preview)
        {
            const SceneViewMode viewMode = m_ViewMode;
            m_ViewMode = SceneViewMode::Lit;
            SubmitModel(m_Preview->Model, m_Preview->Transform);
            m_ViewMode = viewMode;
            AppendModelDraws(m_Preview->Model, m_Preview->Transform, 0, previewDraws);
            m_Preview.reset();
        }
        {
            // One snapshot for the deferred pass, independent of the number of submissions.
            const RasterState saved;
            // The viewport alpha belongs to the target, even when a project shader returns alpha < 1.
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
            DrawShaderMaterials(m_ModelDraws);
            for (const auto& draw : m_GeometryDraws)
                DrawModelGeometry(draw);
            saved.Apply();
        }
        m_ModelDraws.clear();
        m_GeometryDraws.clear();
        if (!m_Highlight.empty())
        {
            m_Outline.Draw(m_Highlight, viewProjection, {m_ViewportWidth, m_ViewportHeight}, {1.0f, 0.62f, 0.15f, 1.0f});
            m_Highlight.clear();
        }
        m_Outline.Draw(previewDraws, viewProjection, {m_ViewportWidth, m_ViewportHeight}, {0.35f, 0.7f, 1.0f, 1.0f});
        m_FrameBuffer->Unbind();
        m_Active = false;
    }

    PickHit SceneRenderer::PickSurface(const std::vector<MeshDraw>& draws,
                                       const glm::mat4&             viewProjection,
                                       glm::uvec2                   pixel)
    {
        return m_Picker.PickSurface(draws, viewProjection, {m_ViewportWidth, m_ViewportHeight}, pixel);
    }

    void SceneRenderer::SetPreview(const ProductModelComponent& model, const glm::mat4& transform)
    {
        m_Preview = Preview {model, transform};
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
