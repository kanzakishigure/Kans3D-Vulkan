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
#include "Kans3D/RHI/OpenGL/OpenGLInterop.h"
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

    SceneRenderer::~SceneRenderer()
    {
        // RHI-MIGRATION: delete the attachment-only FBO used by remaining immediate passes.
        RenderHardware::OpenGLInterop::DeleteSceneFramebuffer(m_InteropFramebuffer);
        if (m_Active)
            m_Device->EndFrame();
        m_Device->WaitIdle();
    }

    void SceneRenderer::DrawGeometry(const Ref<VertexArray>& geometry)
    {
        geometry->Bind();
        OpenGLRenderCommand::DrawIndexed(geometry);
        geometry->Unbind();
    }

    void SceneRenderer::DrawMesh(const GpuMesh& mesh)
    {
        // RHI-MIGRATION: auxiliary geometry still uses the immutable VAO view.
        mesh.VertexArray->Bind();
        OpenGLRenderCommand::DrawIndexed(mesh.VertexArray);
        ++m_Stats.DrawCalls;
        m_Stats.Triangles += mesh.IndexCount / 3;
    }

    void SceneRenderer::SetAssetManager(AssetManager* assets)
    {
        m_AssetCache = assets ? CreateScope<RenderAssetCache>(*assets, m_Device) : nullptr;
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
                                        model.ResolveMaterial(instance.Mesh, mesh->Material, mesh->AuthoredMaterial),
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

        EngineFrameUniforms frame;
        frame.ViewProjection =
            m_SceneInfo.sceneCamera.camera.GetProjectionMatrix() * m_SceneInfo.sceneCamera.viewMatrix;
        const auto coordinates = m_Device->GetCoordinateConventions();
        glm::mat4  clipConversion(1);
        if (coordinates.ClipY == RenderHardware::ClipYDirection::Down)
            clipConversion[1][1] = -1;
        if (coordinates.Depth == RenderHardware::DepthRange::ZeroToOne)
        {
            clipConversion[2][2] = 0.5f;
            clipConversion[3][2] = 0.5f;
        }
        frame.ViewProjection = clipConversion * frame.ViewProjection;
        frame.CameraPosition = glm::vec4(m_SceneInfo.sceneCamera.Position, 1.0f);
        for (uint32_t i = 0; i < EngineLightCount; ++i)
        {
            frame.LightPositions[i] = glm::vec4(m_SceneInfo.pointLight.Position, 1.0f);
            frame.LightColors[i]    = glm::vec4(m_SceneInfo.pointLight.Ambient_Intensity, 1.0f);
            frame.LightDiffuse[i]   = glm::vec4(m_SceneInfo.pointLight.Diffuse_Intensity, 1.0f);
        }
        frame.Time = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_StartTime).count();
        using namespace RenderHardware;
        const size_t alignment   = m_Device->GetBufferBindingAlignment(BindingType::UniformBuffer);
        const auto   align       = [alignment](size_t n) { return (n + alignment - 1) / alignment * alignment; };
        const size_t frameStride = align(sizeof(frame)), objectStride = align(sizeof(EngineObjectUniforms));
        // Each draw owns disjoint immutable ranges until command/frame retirement.
        if (ready.empty())
            return;
        auto frames  = m_Device->CreateBuffer({frameStride * ready.size(), BufferUsage::Uniform, MemoryUsage::Upload});
        auto objects = m_Device->CreateBuffer({objectStride * ready.size(), BufferUsage::Uniform, MemoryUsage::Upload});
        // RHI-MIGRATION: legacy auxiliary shaders use texture parameters, so restore their sampler bindings.
        const auto savedSamplers = OpenGLInterop::SaveSamplers();
        auto       commands      = m_Device->CreateCommandList();
        commands->BeginRendering(SceneTarget(LoadOp::Load));
        commands->SetViewport({0, 0, float(m_ViewportWidth), float(m_ViewportHeight), 0, 1});
        commands->SetScissor({0, 0, m_ViewportWidth, m_ViewportHeight});
        if (!m_IBLSampler)
        {
            SamplerDesc desc;
            desc.U = desc.V = desc.W = AddressMode::ClampToEdge;
            m_IBLSampler             = m_Device->CreateSampler(desc);
        }
        std::array<Handle<RenderHardware::Texture>, 3> environment;
        const std::array<Ref<Kans::Texture>, 3>        legacy {
            m_Environment.IrradianceMap, m_Environment.RadianceMap, m_BrdfLUT};
        for (size_t i = 0; i < 3; ++i)
        {
            const auto dimension = i == 2 ? TextureDimension::Texture2D : TextureDimension::Cube;
            if (legacy[i])
            {
                // RHI-MIGRATION: borrow legacy IBL precomputation outputs and retain their owners.
                environment[i] =
                    OpenGLInterop::BorrowTexture(*m_Device, legacy[i]->GetRenererID(), dimension, legacy[i]);
            }
            else
            {
                TextureDesc desc;
                desc.Dimension   = dimension;
                desc.PixelFormat = Format::RGBA32Float;
                desc.Usage     = RenderHardware::TextureUsage::Sampled | RenderHardware::TextureUsage::CopyDestination;
                environment[i] = m_Device->CreateTexture(desc);
                const std::array<float, 4> black {0, 0, 0, 1};
                auto upload = m_Device->CreateBuffer({sizeof(black), BufferUsage::CopySource, MemoryUsage::Upload});
                upload->Write(0, black.data(), sizeof(black));
                auto copy = m_Device->CreateCommandList();
                for (uint32_t face = 0; face < (i == 2 ? 1u : 6u); ++face)
                    copy->CopyBufferToTexture(upload, 0, environment[i], {0, face});
                m_Device->Submit(copy);
            }
        }
        size_t               range = 0, frameRange = 0;
        std::optional<float> uploadedDebugView;
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
            const auto&    render      = item.Render;
            const bool     transparent = render.AlphaMode == MaterialAlphaMode::Blend;
            const uint32_t key         = uint32_t(transparent) | (uint32_t(render.DoubleSided) << 1) |
                                         (uint32_t(draw.ViewMode == SceneViewMode::Wireframe) << 2) |
                                         (uint32_t(draw.Stencil == ModelStencilMode::WriteReference) << 3);
            auto&          pipeline    = shader->Pipelines[key];
            if (!pipeline)
            {
                PipelineDesc desc;
                desc.Program                      = shader->Program;
                desc.GroupLayouts                 = {shader->EngineLayout, shader->MaterialLayout};
                desc.VertexBindings               = mesh->VertexBindings;
                desc.Attributes                   = mesh->Attributes;
                desc.ColorFormats                 = {Format::RGBA8Unorm, Format::RGBA8Unorm};
                desc.HasDepthStencil              = true;
                desc.DepthStencilFormat           = Format::Depth24Stencil8;
                desc.DepthStencil.DepthTest       = true;
                desc.DepthStencil.DepthWrite      = !transparent;
                desc.DepthStencil.DepthCompare    = CompareOp::Less;
                desc.DepthStencil.StencilTest     = draw.Stencil == ModelStencilMode::WriteReference;
                desc.DepthStencil.Front.Reference = desc.DepthStencil.Back.Reference = 1;
                desc.DepthStencil.Front.Pass = desc.DepthStencil.Back.Pass = StencilOp::Replace;
                desc.Rasterizer.Front =
                    coordinates.ClipY == ClipYDirection::Up ? FrontFace::CounterClockwise : FrontFace::Clockwise;
                desc.Rasterizer.Cull = render.DoubleSided ? CullMode::None : CullMode::Back;
                desc.Rasterizer.Polygon =
                    draw.ViewMode == SceneViewMode::Wireframe ? PolygonMode::Line : PolygonMode::Fill;
                BlendState blend;
                blend.Enabled          = transparent;
                blend.WriteMask        = 7;
                blend.SourceColor      = BlendFactor::SourceAlpha;
                blend.DestinationColor = BlendFactor::OneMinusSourceAlpha;
                blend.SourceAlpha      = BlendFactor::Zero;
                blend.DestinationAlpha = BlendFactor::One;
                desc.Blends            = {blend, blend};
                pipeline               = m_Device->CreatePipeline(desc);
            }
            frame.DebugView = draw.ViewMode == SceneViewMode::BaseColor ? 1.0f :
                              draw.ViewMode == SceneViewMode::Normals   ? 2.0f :
                                                                          0.0f;
            if (!uploadedDebugView || *uploadedDebugView != frame.DebugView)
            {
                if (uploadedDebugView)
                    ++frameRange;
                frames->Write(frameRange * frameStride, &frame, sizeof(frame));
                uploadedDebugView = frame.DebugView;
            }
            EngineObjectUniforms object;
            object.Transform      = draw.Transform;
            object.NormalMatrix   = glm::transpose(glm::inverse(draw.Transform));
            object.MaterialRender = {float(render.AlphaMode), render.AlphaCutoff, render.DoubleSided ? 1.0f : 0.0f, 0};
            objects->Write(range * objectStride, &object, sizeof(object));
            BindGroupDesc engine;
            engine.Layout = shader->EngineLayout;
            for (const auto& binding : shader->EngineResources)
            {
                BindGroupEntry entry;
                entry.Slot = binding.Binding;
                if (binding.Binding == uint32_t(EngineBinding::Frame))
                {
                    entry.BufferResource = frames;
                    entry.Offset         = frameRange * frameStride;
                    entry.Bytes          = sizeof(frame);
                }
                else if (binding.Binding == uint32_t(EngineBinding::Object))
                {
                    entry.BufferResource = objects;
                    entry.Offset         = range * objectStride;
                    entry.Bytes          = sizeof(object);
                }
                else
                {
                    entry.TextureResource = environment.at(binding.Binding - 2);
                    entry.SamplerResource = m_IBLSampler;
                }
                engine.Entries.push_back(std::move(entry));
            }
            commands->BindPipeline(pipeline);
            commands->BindGroup(m_Device->CreateBindGroup(engine));
            commands->BindGroup(m_AssetCache->GetMaterialGroup(*shader, *bindings));
            commands->BindVertexBuffers({{0, mesh->Vertices, 0}});
            commands->BindIndexBuffer(mesh->Indices, IndexType::Uint32);
            commands->DrawIndexed(mesh->IndexCount);
            ++m_Stats.DrawCalls;
            m_Stats.Triangles += mesh->IndexCount / 3;
            ++range;
        }
        commands->EndRendering();
        m_Device->Submit(commands);
        // RHI-MIGRATION: restore sampler state before legacy geometry/selection/Renderer2D.
        OpenGLInterop::RestoreSamplers(savedSamplers);
        // RHI-MIGRATION: resume immediate geometry/outline on the shared scene attachments.
        OpenGLInterop::BindSceneFramebuffer(m_InteropFramebuffer, m_ViewportWidth, m_ViewportHeight);
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
        // RHI-MIGRATION: acquire the device once alongside this context's scene renderer.
        m_Device = RenderHardware::OpenGLInterop::AcquireDevice();
        ResizeTarget();
    }

    RenderHardware::RenderingDesc SceneRenderer::SceneTarget(RenderHardware::LoadOp load) const
    {
        RenderHardware::RenderingDesc desc;
        for (const auto& color : m_Colors)
            desc.Colors.push_back({color, {}, load, RenderHardware::StoreOp::Store, {0.02f, 0.02f, 0.02f, 1}});
        desc.Depth.Target = m_Depth;
        desc.Depth.Load   = load;
        return desc;
    }
    void SceneRenderer::ResizeTarget()
    {
        using namespace RenderHardware;
        // RHI-MIGRATION: recreate the legacy attachment view on resize.
        OpenGLInterop::DeleteSceneFramebuffer(m_InteropFramebuffer);
        m_InteropFramebuffer = 0;
        m_Colors.clear();
        m_Depth.reset();
        TextureDesc desc;
        desc.Width       = m_ViewportWidth;
        desc.Height      = m_ViewportHeight;
        desc.Usage       = RenderHardware::TextureUsage::RenderTarget | RenderHardware::TextureUsage::Sampled |
                           RenderHardware::TextureUsage::CopySource;
        m_Colors         = {m_Device->CreateTexture(desc), m_Device->CreateTexture(desc)};
        desc.PixelFormat = Format::Depth24Stencil8;
        desc.Usage       = RenderHardware::TextureUsage::DepthStencil | RenderHardware::TextureUsage::Sampled |
                           RenderHardware::TextureUsage::CopySource;
        m_Depth          = m_Device->CreateTexture(desc);
        // RHI-MIGRATION: target attachments stay RHI-owned; only the legacy FBO is native.
        m_InteropFramebuffer = OpenGLInterop::CreateSceneFramebuffer(m_Colors, m_Depth);
    }
    uintptr_t SceneRenderer::GetOutput(uint32_t index) const
    {
        if (index > m_Colors.size() + 1)
            return uint32_t(-1);
        return m_Device->GetImGuiTextureID(index < m_Colors.size() ? m_Colors[index] : m_Depth);
    }

    void SceneRenderer::PrepareEnvironment(EnvironmentPrecomputeSizes sizes)
    {
        const auto library =
            m_Specification.AuxiliaryShaders ? m_Specification.AuxiliaryShaders : Renderer::GetShaderLibrary();
        OpenGLRenderCommand::GetError();
        // prepareSceneEnv
        OpenGLRenderCommand::EnableCullFace(false);
        OpenGLRenderCommand::Clear();

        {
            FrameBufferSpecification frameBufferSpec;
            frameBufferSpec.Width                   = sizes.Cubemap;
            frameBufferSpec.Height                  = sizes.Cubemap;
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
            OpenGLShader* hdrToCubeMapShader = static_cast<OpenGLShader*>(library->Get("HdrToCubeMap").get());

            TextureSpecification envCubemapspec;
            envCubemapspec.Width  = sizes.Cubemap;
            envCubemapspec.Height = sizes.Cubemap;
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
            irradianceCubemapspec.Width        = sizes.Irradiance;
            irradianceCubemapspec.Height       = sizes.Irradiance;
            irradianceCubemapspec.Format       = RHIFormat::RHI_FORMAT_R16G16B16_SFLOAT;
            irradianceCubemapspec.Wrap         = RHISamplerAddressMode::RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            irradianceCubemapspec.Minf         = RHIFilter::RHI_FILTER_CUBIC_EXT;
            irradianceCubemapspec.Maxf         = RHIFilter::RHI_FILTER_LINEAR;
            irradianceCubemapspec.GenerateMips = true;
            Ref<TextureCube> irradiancemap     = TextureCube::Create(irradianceCubemapspec);
            framebuffer->Resize(sizes.Irradiance, sizes.Irradiance);

            OpenGLShader* genIrradianceShader = static_cast<OpenGLShader*>(library->Get("genIrradiance").get());
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
            prefilterMapspec.Width        = sizes.Prefilter;
            prefilterMapspec.Height       = sizes.Prefilter;
            prefilterMapspec.Wrap         = RHISamplerAddressMode::RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            prefilterMapspec.Minf         = RHIFilter::RHI_FILTER_CUBIC_IMG;
            prefilterMapspec.Maxf         = RHIFilter::RHI_FILTER_LINEAR;
            prefilterMapspec.GenerateMips = true;
            prefilterMapspec.Format       = RHIFormat::RHI_FORMAT_R16G16B16_SFLOAT;

            Ref<TextureCube> prefilterMap = TextureCube::Create(prefilterMapspec);
            OpenGLRenderCommand::GetError();
            OpenGLShader* prefilterShader = static_cast<OpenGLShader*>(library->Get("prefilter").get());
            prefilterShader->Bind();
            prefilterShader->UploadUniformInt("environmentMap", 2);
            prefilterShader->UploadUniformMat4("projection", captureProjection);
            envCubemap->Bind(2);

            uint32_t maxMipLevels = 5;
            for (unsigned int mip = 0; mip < maxMipLevels; ++mip)
            {
                // 调整 ViewPort大小,保证和 mip-level 维持统一
                unsigned int mipWidth  = static_cast<unsigned int>(sizes.Prefilter * std::pow(0.5, mip));
                unsigned int mipHeight = static_cast<unsigned int>(sizes.Prefilter * std::pow(0.5, mip));
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
            brdfLUTspec.Width  = sizes.BrdfLUT;
            brdfLUTspec.Height = sizes.BrdfLUT;
            brdfLUTspec.Wrap   = RHISamplerAddressMode::RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            brdfLUTspec.Minf   = RHIFilter::RHI_FILTER_LINEAR;
            brdfLUTspec.Maxf   = RHIFilter::RHI_FILTER_LINEAR;
            brdfLUTspec.Format = RHIFormat::RHI_FORMAT_R16G16_SFLOAT;

            Ref<Texture2D> brdfLUTTexture = Texture2D::Create(brdfLUTspec);

            framebuffer->Resize(sizes.BrdfLUT, sizes.BrdfLUT);
            framebuffer->Bind();
            framebuffer->BindTextureToColorAttachMent(brdfLUTTexture);

            OpenGLShader* brdfShader = static_cast<OpenGLShader*>(library->Get("brdf").get());
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
        m_Device->BeginFrame();
        // RHI-MIGRATION: preserve native raster state for the immediate grid/skybox passes.
        const RasterState saved;
        auto              commands = m_Device->CreateCommandList();
        commands->BeginRendering(SceneTarget(RenderHardware::LoadOp::Clear));
        commands->EndRendering();
        m_Device->Submit(commands);
        saved.Apply();
        // RHI-MIGRATION: grid and skybox draw immediately between BeginScene and EndScene.
        RenderHardware::OpenGLInterop::BindSceneFramebuffer(m_InteropFramebuffer, m_ViewportWidth, m_ViewportHeight);
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
        if (m_Preview)
        {
            const SceneViewMode viewMode = m_ViewMode;
            m_ViewMode = SceneViewMode::Lit;
            SubmitModel(m_Preview->Model, m_Preview->Transform);
            m_ViewMode = viewMode;
        }
        {
            // RHI-MIGRATION: restore native raster state after the remaining immediate geometry passes.
            const RasterState saved;
            DrawShaderMaterials(m_ModelDraws);
            // RHI-MIGRATION: legacy geometry shares the target's alpha-preserving write mask.
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
            for (const auto& draw : m_GeometryDraws)
                DrawModelGeometry(draw);
            saved.Apply();
        }
        m_ModelDraws.clear();
        m_GeometryDraws.clear();
        if (!m_Highlight.empty())
        {
            // Material/geometry passes may replace cached meshes. Resolve only at the outline draw.
            std::vector<MeshDraw> draws;
            for (const auto& model : m_Highlight)
                AppendModelDraws(model.Model, model.Transform, 0, draws);
            m_Outline.Draw(draws, viewProjection, {m_ViewportWidth, m_ViewportHeight}, {1.0f, 0.62f, 0.15f, 1.0f});
            m_Highlight.clear();
        }
        if (m_Preview)
        {
            // Resolve after the highlight too: collecting its models can update the cache.
            std::vector<MeshDraw> draws;
            AppendModelDraws(m_Preview->Model, m_Preview->Transform, 0, draws);
            m_Outline.Draw(draws, viewProjection, {m_ViewportWidth, m_ViewportHeight}, {0.35f, 0.7f, 1.0f, 1.0f});
            m_Preview.reset();
        }
        // RHI-MIGRATION: release the shared legacy target after auxiliary passes.
        RenderHardware::OpenGLInterop::BindSceneFramebuffer(0, m_ViewportWidth, m_ViewportHeight);
        m_Device->EndFrame();
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
        m_Preview = ModelDraw {model, transform};
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
        ResizeTarget();
    }

    void SceneRenderer::SubmitGrid(float size, float scale, glm::mat4 transform)
    {
        const auto library =
            m_Specification.AuxiliaryShaders ? m_Specification.AuxiliaryShaders : Renderer::GetShaderLibrary();
        auto shader = static_cast<OpenGLShader*>(library->Get("GridShader").get());
        shader->Bind();

        glm::mat4 viewprojection =
            m_SceneInfo.sceneCamera.camera.GetProjectionMatrix() * m_SceneInfo.sceneCamera.viewMatrix;
        shader->UploadUniformMat4("U_ViewProjection", viewprojection);
        shader->UploadUniformMat4("U_Transform", transform);

        shader->UploadUniformFloat("U_Settings.Scale", scale);
        shader->UploadUniformFloat("U_Settings.Size", size);
        auto VA = m_Specification.AuxiliaryShaders ? MeshFactory::CreatQuad(glm::vec2(1.0f)) : Renderer::GetQuad();
        VA->Bind();
        OpenGLRenderCommand::DrawIndexed(VA);
        VA->Unbind();
    }

    void SceneRenderer::SubmitSkybox(Ref<TextureCube> skybox)
    {
        const auto library =
            m_Specification.AuxiliaryShaders ? m_Specification.AuxiliaryShaders : Renderer::GetShaderLibrary();
        auto shader = static_cast<OpenGLShader*>(library->Get("skybox").get());
        shader->Bind();
        shader->UploadUniformMat4("U_Projection", m_SceneInfo.sceneCamera.camera.GetProjectionMatrix());
        shader->UploadUniformMat4("U_View", m_SceneInfo.sceneCamera.viewMatrix);
        shader->UploadUniformInt("environmentMap", 1);
        skybox->Bind(1);
        DrawGeometry(MeshFactory::GetCube());
    }

} // namespace Kans
