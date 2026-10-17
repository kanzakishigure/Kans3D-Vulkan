#include "kspch.h"
#include "Kans3D/Scene/Entity.h"

#include <string>
#include "Kans3D/Core/Log/Log.h"
#include "Kans3D/Renderer/Features/Editor/EditorRenderFeatures.h"
#include "Kans3D/Renderer/Features/MaterialPassFeature.h"
#include "Kans3D/Renderer/RenderPipeline.h"
#include "Kans3D/Renderer/Renderer.h"
#include "Kans3D/Renderer/Renderer2D.h"
#include "Kans3D/Renderer/Resource/InternalShader.h"
#include "Kans3D/Renderer/Resource/MaterialPass.h"
#include "Kans3D/Renderer/Resource/SceneShaders.h"
#include "Kans3D/Renderer/SceneRenderer.h"

namespace Kans
{
    namespace
    {
        using namespace RenderHardware;
        struct SceneDrawData
        {
            glm::mat4 ViewProjection {1}, Transform {1}, DeviceClip {1};
            glm::vec4 ViewPos {0}, Settings {0};
        };
        template<class T>
        Handle<BindGroup> DrawGroup(Device&                         device,
                                    const Handle<BindGroupLayout>&  layout,
                                    const T&                        data,
                                    Handle<RenderHardware::Texture> texture = {},
                                    Handle<Sampler>                 sampler = {})
        {
            auto buffer = device.CreateBuffer({sizeof(T), BufferUsage::Uniform, MemoryUsage::Upload});
            buffer->Write(0, &data, sizeof(T));
            BindGroupDesc group;
            group.Layout = layout;
            group.Entries.push_back({0, buffer, 0, sizeof(T), {}, {}});
            if (texture)
                group.Entries.push_back({1, {}, 0, 0, std::move(texture), std::move(sampler)});
            return device.CreateBindGroup(group);
        }
        template<class F>
        void ForEachSection(const ModelInstance& instance, const GpuMesh& mesh, F&& action)
        {
            const size_t first = instance.AllSections ? 0 : instance.Section;
            const size_t end   = instance.AllSections ? mesh.Sections.size() : first + 1;
            for (size_t i = first; i < end && i < mesh.Sections.size(); ++i)
            {
                auto part        = instance;
                part.Section     = static_cast<uint32_t>(i);
                part.AllSections = false;
                action(part, mesh.Sections[i]);
            }
        }
    } // namespace

    SceneRenderer::SceneRenderer(RenderScene* scene, SceneRendererSpecification spec) :
        m_RenderScene(scene), m_Specification(spec)
    {

        m_Features.Register("MaterialOutline",
                            std::make_unique<MaterialPassFeature>("Outline", RenderExtensionPoint::AfterTransparent));
        Init();
    }

    SceneRenderer::~SceneRenderer()
    {
        if (m_Active)
        {
            Renderer2D::ClearTarget();
            m_Renderer->GetDevice()->EndFrame();
        }
        // Graph lists and pooled textures must release while our device is still alive.
        m_Graph         = {};
        m_Target.reset();
        m_View          = {};
        m_Features      = {};
        m_Renderer->GetDevice()->WaitIdle();
    }

    void SceneRenderer::SetAssetManager(AssetManager* assets)
    {
        m_AssetCache = assets ? m_Renderer->AcquireAssetCache(*assets) : nullptr;
        m_RenderScene->GetLogicScene()->SetPrefabAssetManager(assets);
    }

    void SceneRenderer::PrepareDrawLists()
    {
        for (auto& draw : m_View.Draws)
        {
            const auto* mesh = m_AssetCache->GetMesh(draw.Mesh);
            const auto* bindings =
                m_AssetCache->GetMaterialBindings(draw.Material, draw.ViewMode != SceneViewMode::Lit);
            draw.Ready = mesh && draw.Section < mesh->Sections.size() && bindings;
            if (draw.Ready)
            {
                draw.LocalBounds = mesh->Sections[draw.Section].LocalBounds;
                draw.Render      = bindings->Render;
            }
        }
        ExtractDrawLists(m_View);
    }

    bool SceneRenderer::HasMaterialPass(const RenderView& view, const std::string& tag)
    {
        if (!m_AssetCache)
            return false;
        for (const auto& item : view.MaterialOrder)
        {
            const auto& draw = view.Draws[item.DrawIndex];
            if (!draw.Ready)
                continue;
            const auto* bindings =
                m_AssetCache->GetMaterialBindings(draw.Material, draw.ViewMode != SceneViewMode::Lit);
            const auto* shader = bindings ? m_AssetCache->GetShader(bindings->Shader) : nullptr;
            if (shader && std::any_of(shader->Passes.begin(), shader->Passes.end(), [&](const auto& pass) {
                    return pass.Tag == tag;
                }))
                return true;
        }
        return false;
    }

    void SceneRenderer::DrawShaderMaterials(
        RenderHardware::CommandList&                                                       commandList,
        const std::function<void(const RenderHardware::Handle<RenderHardware::Texture>&)>& sampled,
        const std::string&                                                                 tag,
        const RenderView*                                                                  suppliedView,
        RenderHardware::Handle<RenderHardware::CommandList>*                               finalPost)
    {
        const auto& view = suppliedView ? *suppliedView : m_View;
        if (!m_AssetCache || view.MaterialOrder.empty())
            return;
        const auto&         ready    = view.MaterialOrder;
        auto*               commands = &commandList;
        EngineFrameUniforms frame    = MakeLightingUniforms(m_SceneInfo);
        frame.ViewProjection =
            m_SceneInfo.sceneCamera.camera.GetProjectionMatrix() * m_SceneInfo.sceneCamera.viewMatrix;
        const auto coordinates = m_Renderer->GetDevice()->GetCoordinateConventions();
        frame.ViewProjection   = DeviceProjection(frame.ViewProjection, coordinates);
        frame.CameraPosition = glm::vec4(m_SceneInfo.sceneCamera.Position, 1.0f);
        frame.Time = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_StartTime).count();
        using namespace RenderHardware;
        const size_t alignment   = m_Renderer->GetDevice()->GetBufferBindingAlignment(BindingType::UniformBuffer);
        const auto   align       = [alignment](size_t n) { return (n + alignment - 1) / alignment * alignment; };
        const size_t frameStride = align(sizeof(frame)), objectStride = align(sizeof(EngineObjectUniforms));
        // Each draw owns disjoint immutable ranges until command/frame retirement.
        if (ready.empty())
            return;
        auto frames = m_Renderer->GetDevice()->CreateBuffer(
            {frameStride * ready.size(), BufferUsage::Uniform, MemoryUsage::Upload});
        auto objects = m_Renderer->GetDevice()->CreateBuffer(
            {objectStride * ready.size(), BufferUsage::Uniform, MemoryUsage::Upload});
        bool rendering = false, hdr = false;
        auto resolve = [&](RenderHardware::CommandList& output) {
            m_Renderer->GetToneMapping().Record(
                *m_Renderer->GetDevice(), output, m_Target->GetHDR(), m_Target->GetColors(), frame.LightingInfo.y);
        };
        auto beginTarget = [&](bool nextHDR) {
            if (rendering && nextHDR == hdr)
                return;
            if (rendering)
                commands->EndRendering();
            if (hdr)
                resolve(*commands);
            if (nextHDR)
                m_Renderer->GetToneMapping().Record(*m_Renderer->GetDevice(),
                                                    *commands,
                                                    m_Target->GetColors()[0],
                                                    {m_Target->GetHDR()},
                                                    frame.LightingInfo.y,
                                                    true);
            hdr = nextHDR;
            commands->BeginRendering(SceneTarget(LoadOp::Load, hdr));
            commands->SetViewport({0, 0, float(m_ViewportWidth), float(m_ViewportHeight), 0, 1});
            commands->SetScissor({0, 0, m_ViewportWidth, m_ViewportHeight});
            rendering = true;
        };
        const auto           environment = GetMaterialEnvironment();
        size_t               range = 0, frameRange = 0;
        std::optional<float> uploadedDebugView;
        const auto           litFrame = frame;
        for (const auto& item : ready)
        {
            const auto& draw = view.Draws[item.DrawIndex];
            // Requests may finish during this pass. Snapshot the state used for sorting,
            // but acquire GPU resources at the draw so a cache replacement cannot dangle a queued pointer.
            const auto* mesh = m_AssetCache->GetMesh(draw.Mesh);
            const auto* bindings =
                m_AssetCache->GetMaterialBindings(draw.Material, draw.ViewMode != SceneViewMode::Lit);
            const auto* shader = bindings ? m_AssetCache->GetShader(bindings->Shader) : nullptr;
            if (!mesh || draw.Section >= mesh->Sections.size() || !bindings || !shader)
                continue;
            if (!tag.empty() &&
                (!draw.Ready || std::none_of(shader->Passes.begin(), shader->Passes.end(), [&](const auto& pass) {
                    return pass.Tag == tag;
                })))
                continue;
            const auto&    render      = item.Render;
            frame                      = litFrame;
            frame.DebugView            = draw.ViewMode == SceneViewMode::BaseColor ? 1.0f :
                                         draw.ViewMode == SceneViewMode::Normals   ? 2.0f :
                                         draw.ViewMode == SceneViewMode::Wireframe ? 3.0f :
                                                                                     0.0f;
            if (draw.ViewMode == SceneViewMode::Wireframe)
            {
                // The wireframe diagnostic intentionally retains its phase-one appearance.
                frame.LightingInfo.x = EngineLightCount;
                for (uint32_t i = 0; i < EngineLightCount; ++i)
                {
                    frame.PointPositions[i] = glm::vec4(m_SceneInfo.pointLight.Position, 1);
                    frame.PointRadiance[i]  = glm::vec4(m_SceneInfo.pointLight.Ambient_Intensity, 0);
                    frame.PointSpecular[i]  = glm::vec4(1);
                }
                frame.DirectionalDirection = frame.DirectionalAmbient = glm::vec4(0);
            }
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
            MaterialPassData pass;
            pass.Frame              = frames;
            pass.Object             = objects;
            pass.FrameOffset        = frameRange * frameStride;
            pass.ObjectOffset       = range * objectStride;
            pass.Environment        = environment;
            pass.EnvironmentSampler = m_Renderer->GetMaterialEnvironmentSampler();
            // Debug views, including wireframe's lit shading, retain display output.
            const bool nextHDR =
                shader->OutputSpace == ShaderOutputSpace::LinearHDR && draw.ViewMode == SceneViewMode::Lit;
            beginTarget(nextHDR);
            pass.ColorFormats       = {nextHDR ? Format::RGBA16Float : Format::RGBA8Unorm, Format::RGBA8Unorm};
            pass.Wireframe          = draw.ViewMode == SceneViewMode::Wireframe;
            pass.WriteStencil       = draw.Stencil == ModelStencilMode::WriteReference;
            pass.Render             = render;
            pass.Section            = draw.Section;
            if (tag.empty())
                RecordMaterialDraw(*m_AssetCache, *commands, *shader, *bindings, *mesh, pass);
            else
                for (const auto& extra : shader->Passes)
                    if (extra.Tag == tag)
                    {
                        RecordMaterialDraw(*m_AssetCache, *commands, *shader, *bindings, *mesh, pass, &extra);
                        ++m_Stats.DrawCalls;
                        m_Stats.Triangles += mesh->Sections[draw.Section].IndexCount / 3;
                    }
            if (sampled)
                for (const auto& image : bindings->Images)
                    sampled(image);
            if (tag.empty())
            {
                ++m_Stats.DrawCalls;
                m_Stats.Triangles += mesh->Sections[draw.Section].IndexCount / 3;
            }
            ++range;
        }
        if (rendering)
            commands->EndRendering();
        if (hdr)
        {
            if (finalPost)
            {
                *finalPost = m_Renderer->GetDevice()->CreateCommandList();
                resolve(**finalPost);
            }
            else
                resolve(*commands);
        }
    }

    std::array<RenderHardware::Handle<RenderHardware::Texture>, 3> SceneRenderer::GetMaterialEnvironment()
    {
        RefreshEnvironment();
        return m_Renderer->GetMaterialEnvironment(*m_EnvironmentLighting);
    }

    void SceneRenderer::SubmitMesh(const ViewMeshSubmission& submission)
    {
        if (!m_AssetCache)
            return;
        const auto* mesh = m_AssetCache->GetMesh(submission.Mesh.Mesh);
        ExtractMeshDraws(m_View, submission, m_ViewMode, mesh);
    }

    void SceneRenderer::AppendEntityDraws(Scene& scene, UUID entity, uint32_t id, std::vector<MeshDraw>& draws)
    {
        if (!m_AssetCache)
            return;
        const auto selected = scene.GetEntityByUUID(entity);
        if (!selected)
            return;
        for (auto part : scene.GetMeshEntities(selected))
        {
            const auto& component = part.GetComponent<MeshComponent>();
            if (const auto* mesh = m_AssetCache->GetMesh(component.Mesh))
                for (size_t i = 0; i < mesh->Sections.size(); ++i)
                    if (!component.Section || *component.Section == i)
                        draws.push_back({mesh, scene.GetWorldTransform(part), id, uint32_t(i)});
        }
    }

    ScenePickHit SceneRenderer::PickSceneSurface(Scene& scene, const glm::mat4& viewProjection, glm::uvec2 pixel)
    {
        if (m_AssetCache)
            scene.UpdatePrefabs(m_AssetCache->GetAssetManager());
        const auto            entities = scene.GetMeshEntities();
        std::vector<MeshDraw> draws;
        for (size_t i = 0; i < entities.size(); ++i)
        {
            auto        entity    = entities[i];
            const auto& component = entity.GetComponent<MeshComponent>();
            if (!m_AssetCache)
                break;
            if (const auto* mesh = m_AssetCache->GetMesh(component.Mesh))
                for (size_t section = 0; section < mesh->Sections.size(); ++section)
                    if (!component.Section || *component.Section == section)
                        draws.push_back({mesh, scene.GetWorldTransform(entity), uint32_t(i + 1), uint32_t(section)});
        }
        const auto hit = m_Renderer->GetPickingService().PickSurface(
            draws, viewProjection, {m_ViewportWidth, m_ViewportHeight}, pixel);
        if (!hit.ID || hit.ID > entities.size())
            return {};
        const auto entity = entities[hit.ID - 1];
        const auto root   = scene.GetPrefabInstanceRoot(entity);
        return {entity.GetUUID(), root ? root.GetUUID() : UUID(0), hit.Position};
    }

    void SceneRenderer::SubmitMeshGeometry(const ViewGeometryDraw& draw) { m_View.Geometry.push_back(draw); }

    void SceneRenderer::DrawModelGeometry(const GeometryDraw& draw, RenderHardware::CommandList& commandList)
    {
        using namespace RenderHardware;
        if (!m_AssetCache)
            return;
        ModelInstances direct;
        if (draw.Mesh.IsValid())
            direct.Instances.push_back({draw.Mesh, glm::mat4(1), draw.Section.value_or(0), {}, !draw.Section});
        const ModelInstances* instances = &direct;
        if (!instances)
            return;
        const bool  normals  = draw.ShaderName == "DebugNormalShader";
        const char* vertex   = normals                            ? SceneShaders::NormalVertex :
                               draw.ShaderName == "OutLineShader" ? SceneShaders::OutlineVertex :
                               draw.ShaderName == "StencilShader" ? SceneShaders::StencilVertex :
                                                                    nullptr;
        const char* fragment = normals                            ? SceneShaders::NormalFragment :
                               draw.ShaderName == "OutLineShader" ? SceneShaders::OutlineFragment :
                                                                    SceneShaders::StencilFragment;
        if (!vertex)
            return;
        PipelineDesc desc = m_Renderer->ScenePipeline(draw.ShaderName.c_str(), vertex, fragment, false);
        if (!desc.Program)
            return;
        desc.DepthStencil.DepthCompare =
            draw.Mode == ModelGeometryMode::StencilOutline ? CompareOp::LessEqual : CompareOp::Less;
        desc.DepthStencil.StencilTest   = draw.Mode == ModelGeometryMode::StencilOutline;
        desc.DepthStencil.Front.Compare = desc.DepthStencil.Back.Compare = CompareOp::NotEqual;
        desc.DepthStencil.Front.Reference = desc.DepthStencil.Back.Reference = 1;
        desc.DepthStencil.Front.WriteMask = desc.DepthStencil.Back.WriteMask = 0;
        desc.Rasterizer.Cull = draw.Mode == ModelGeometryMode::BackFaces ? CullMode::Front : CullMode::None;
        if (normals)
        {
            desc.Primitive      = Topology::LineList;
            desc.VertexBindings = {{0, 28, false}};
            desc.Attributes     = {
                {0, 0, 0, VertexFormat::Float3}, {1, 0, 12, VertexFormat::Float3}, {2, 0, 24, VertexFormat::Float}};
        }
        else
        {
            desc.VertexBindings = {{0, 64, false}};
            desc.Attributes     = {{0, 0, 0, VertexFormat::Float3},
                                   {1, 0, 12, VertexFormat::Float3},
                                   {2, 0, 24, VertexFormat::Float2},
                                   {3, 0, 32, VertexFormat::Float4},
                                   {4, 0, 48, VertexFormat::Float4}};
        }
        auto  pipeline = m_Renderer->CachedPipeline(desc);
        auto* commands = &commandList;
        commands->BeginRendering(SceneTarget(LoadOp::Load));
        commands->SetViewport({0, 0, float(m_ViewportWidth), float(m_ViewportHeight)});
        commands->BindPipeline(pipeline);
        SceneDrawData data;
        data.DeviceClip     = DeviceProjection(glm::mat4(1), m_Renderer->GetDevice()->GetCoordinateConventions());
        data.ViewProjection = m_SceneInfo.sceneCamera.camera.GetProjectionMatrix() * m_SceneInfo.sceneCamera.viewMatrix;
        data.ViewPos        = glm::vec4(m_SceneInfo.sceneCamera.Position, 1);
        for (const auto& instance : instances->Instances)
            if (const GpuMesh* mesh = m_AssetCache->GetMesh(instance.Mesh))
                ForEachSection(instance, *mesh, [&](const auto& part, const auto& section) {
                    data.Transform = draw.Transform * part.Transform;
                    commands->BindGroup(DrawGroup(*m_Renderer->GetDevice(), desc.GroupLayouts[0], data));
                    commands->BindVertexBuffers({{0, normals ? mesh->NormalLines : mesh->Vertices, 0}});
                    if (normals)
                        commands->Draw(section.IndexCount * 2, 1, section.FirstIndex * 2);
                    else
                    {
                        commands->BindIndexBuffer(mesh->Indices, IndexType::Uint32);
                        commands->DrawIndexed(section.IndexCount, 1, section.FirstIndex);
                    }
                    ++m_Stats.DrawCalls;
                    m_Stats.Triangles += section.IndexCount / 3;
                });
        commands->EndRendering();
    }

    void SceneRenderer::Init()
    {
        m_ViewportWidth  = m_RenderScene->m_ViewportWidth;
        m_ViewportHeight = m_RenderScene->m_ViewportHeight;
        m_Renderer       = Renderer::Acquire();
        m_Outline.SetDevice(m_Renderer->GetDevice());
        m_Target = std::make_unique<ViewTarget>(m_Renderer->GetDevice());
        m_Target->Resize({m_ViewportWidth, m_ViewportHeight});
    }
    RenderHardware::RenderingDesc SceneRenderer::SceneTarget(RenderHardware::LoadOp load, bool hdr) const
    {
        return m_Target->Target(load, hdr);
    }
    uintptr_t SceneRenderer::GetOutput(uint32_t index) const { return m_Target->GetOutput(index); }
    void      SceneRenderer::PrepareEnvironment(EnvironmentPrecomputeSizes sizes)
    {
        auto source         = m_RenderScene->m_EnvironmentPath;
        auto lighting       = m_Renderer->PrepareEnvironment(source, sizes);
        m_EnvironmentSizes  = sizes;
        m_EnvironmentSource = std::move(source);
        if (lighting)
            m_EnvironmentLighting = std::move(lighting);
    }
    void SceneRenderer::RefreshEnvironment() const
    {
        if (!m_EnvironmentSource.empty() && m_EnvironmentSource != m_RenderScene->m_EnvironmentPath)
        {
            m_EnvironmentSource = m_RenderScene->m_EnvironmentPath;
            auto lighting       = m_Renderer->PrepareEnvironment(m_EnvironmentSource, m_EnvironmentSizes);
            if (lighting)
                m_EnvironmentLighting = std::move(lighting);
        }
    }
    void SceneRenderer::UpdateEnvironment(std::string path)
    {
        m_RenderScene->m_EnvironmentPath = std::move(path);
        m_EnvironmentSource              = m_RenderScene->m_EnvironmentPath;
        auto lighting                    = m_Renderer->UpdateEnvironment(m_EnvironmentSource, m_EnvironmentSizes);
        if (lighting)
            m_EnvironmentLighting = std::move(lighting);
    }

    void SceneRenderer::BeginScene(SceneInfo info)
    {

        CORE_ASSERT(m_RenderScene, "Render a invalid Scene with nullptr handle");
        m_Active    = true;
        m_SceneInfo = info;
        m_Stats     = {};
        m_View.Draws.clear();
        m_View.MaterialOrder.clear();
        m_View.Geometry.clear();
        m_View.Meshes.clear();
        m_View.Sprites.clear();
        m_View.Grids.clear();
        m_View.Skyboxes.clear();
        m_View.SpriteBatches.clear();
        m_View.ImmediateOrder.clear();
        m_View.Scene           = info;
        m_View.Viewport        = {m_ViewportWidth, m_ViewportHeight};
        m_View.DebugMode       = m_ViewMode;
        RefreshEnvironment();
        m_View.EnvironmentMaps = {m_EnvironmentLighting->Maps.IrradianceMap, m_EnvironmentLighting->Maps.RadianceMap};
        m_View.BrdfLUT         = m_EnvironmentLighting->BrdfLUT;
        if (m_AssetCache)
            m_AssetCache->BeginFrame();
        m_Renderer->GetDevice()->BeginFrame();
        m_Graph       = {};
        auto pipeline = RenderPipelineRegistry::Create(m_PipelineName);
        pipeline->Begin(m_Graph, m_View, *this);
        m_Graph.Compile();
        m_Graph.ExecuteImportedPrefix(*m_Renderer->GetDevice());
        Renderer2D::SetTarget(m_Renderer->GetDevice(), SceneTarget(RenderHardware::LoadOp::Load));
        Renderer2D::SetSubmissionSink([this](auto commands, auto texture) {
            m_View.ImmediateOrder.push_back({ViewImmediateKind::RecordedSprites, m_View.SpriteBatches.size()});
            m_View.SpriteBatches.push_back({std::move(commands), std::move(texture)});
        });
    }

    void SceneRenderer::SubmitPrefabPreview(const PrefabPreview& preview)
    {
        if (!m_AssetCache)
            return;
        if (const auto* instances = m_AssetCache->GetModel(preview.Prefab))
            for (const auto& instance : instances->Instances)
                if (const auto* mesh = m_AssetCache->GetMesh(instance.Mesh))
                    ForEachSection(instance, *mesh, [&](const auto& part, const auto&) {
                        ExtractMeshDraws(m_View,
                                         {{part.Mesh, {}, part.Section}, preview.Transform * part.Transform},
                                         m_ViewMode,
                                         mesh);
                    });
    }

    void SceneRenderer::AppendPrefabPreviewDraws(const PrefabPreview& preview, std::vector<MeshDraw>& draws)
    {
        if (!m_AssetCache)
            return;
        if (const auto* instances = m_AssetCache->GetModel(preview.Prefab))
            for (const auto& instance : instances->Instances)
                if (const auto* mesh = m_AssetCache->GetMesh(instance.Mesh))
                    ForEachSection(instance, *mesh, [&](const auto& part, const auto&) {
                        draws.push_back({mesh, preview.Transform * part.Transform, 0, part.Section});
                    });
    }

    void SceneRenderer::EndScene()
    {
        m_View.HighlightEntities = std::move(m_EntityHighlight);
        m_View.Preview   = std::move(m_Preview);
        m_Features.Prepare(m_View, *this);
        PrepareDrawLists();
        auto pipeline = RenderPipelineRegistry::Create(m_PipelineName);
        pipeline->Build(m_Graph, m_View, *this);
        m_Graph.Compile();
        m_Graph.Execute(*m_Renderer->GetDevice(), m_Target->GetTransientPool());
        m_Preview.reset();
        Renderer2D::SetSubmissionSink({});
        Renderer2D::ClearTarget();
        m_Renderer->GetDevice()->EndFrame();
        m_Active = false;
    }

    void SceneRenderer::SetPipeline(const std::string& name)
    {
        // Validate immediately, so an unknown pipeline cannot strand an active frame.
        RenderPipelineRegistry::Create(name);
        m_PipelineName = name;
    }

    void SceneRenderer::SubmitSprites(std::vector<ViewSprite> sprites)
    {
        m_View.Sprites = std::move(sprites);
        if (!m_View.Sprites.empty())
            m_View.ImmediateOrder.push_back({ViewImmediateKind::Sprites, 0});
    }

    void SceneRenderer::SetPreview(AssetID prefab, const glm::mat4& transform)
    {
        m_Preview = PrefabPreview {prefab, transform};
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
        m_Target->Resize({width, height});
    }

    void SceneRenderer::SubmitGrid(float size, float scale, glm::mat4 transform, ImmediatePassState state)
    {
        m_View.ImmediateOrder.push_back({ViewImmediateKind::Grid, m_View.Grids.size()});
        m_View.Grids.push_back({size, scale, transform, state});
    }

    void SceneRenderer::DrawGrid(const ViewGrid& grid, RenderHardware::CommandList& commandList)
    {
        const auto  size = grid.Size, scale = grid.Scale;
        const auto& transform = grid.Transform;
        const auto& state     = grid.State;
        using namespace RenderHardware;
        auto desc =
            m_Renderer->ScenePipeline("GridShader", SceneShaders::GridVertex, SceneShaders::GridFragment, false);
        if (!desc.Program)
            return;
        desc.VertexBindings             = {{0, 20, false}};
        desc.Attributes                 = {{0, 0, 0, VertexFormat::Float3}, {1, 0, 12, VertexFormat::Float2}};
        desc.DepthStencil.DepthCompare  = state.DepthCompare;
        desc.DepthStencil.DepthWrite    = state.DepthWrite;
        desc.Blends[0].Enabled          = state.Blend;
        desc.Blends[0].WriteMask        = state.WriteMask;
        desc.Blends[0].SourceAlpha      = BlendFactor::SourceAlpha;
        desc.Blends[0].DestinationAlpha = BlendFactor::OneMinusSourceAlpha;
        desc.Blends[0].SourceColor      = BlendFactor::SourceAlpha;
        desc.Blends[0].DestinationColor = BlendFactor::OneMinusSourceAlpha;
        desc.Blends[1]                  = desc.Blends[0];
        const float    vertices[]       = {-0.5f, 0, -0.5f, 0, 0, 0.5f,  0, -0.5f, 1, 0,
                                           0.5f,  0, 0.5f,  1, 1, -0.5f, 0, 0.5f,  0, 1};
        const uint32_t indices[]        = {0, 1, 2, 2, 3, 0};
        auto vb = m_Renderer->GetDevice()->CreateBuffer({sizeof(vertices), BufferUsage::Vertex, MemoryUsage::Upload});
        auto ib = m_Renderer->GetDevice()->CreateBuffer({sizeof(indices), BufferUsage::Index, MemoryUsage::Upload});
        vb->Write(0, vertices, sizeof(vertices));
        ib->Write(0, indices, sizeof(indices));
        SceneDrawData data;
        data.DeviceClip     = DeviceProjection(glm::mat4(1), m_Renderer->GetDevice()->GetCoordinateConventions());
        data.ViewProjection = m_SceneInfo.sceneCamera.camera.GetProjectionMatrix() * m_SceneInfo.sceneCamera.viewMatrix;
        data.Transform      = transform;
        data.Settings       = {scale, size, 0, 0};
        auto* commands      = &commandList;
        commands->BeginRendering(SceneTarget(LoadOp::Load));
        commands->SetViewport({0, 0, float(m_ViewportWidth), float(m_ViewportHeight)});
        commands->BindPipeline(m_Renderer->CachedPipeline(desc));
        commands->BindGroup(DrawGroup(*m_Renderer->GetDevice(), desc.GroupLayouts[0], data));
        commands->BindVertexBuffers({{0, vb, 0}});
        commands->BindIndexBuffer(ib, IndexType::Uint32);
        commands->DrawIndexed(6);
        commands->EndRendering();
    }

    void SceneRenderer::SubmitSkybox(RenderHardware::Handle<RenderHardware::Texture> skybox, ImmediatePassState state)
    {
        if (!skybox)
            return;
        m_View.ImmediateOrder.push_back({ViewImmediateKind::Skybox, m_View.Skyboxes.size()});
        m_View.Skyboxes.push_back({std::move(skybox), state});
    }

    void SceneRenderer::DrawSkybox(const ViewSkybox& sky, RenderHardware::CommandList& commandList)
    {
        const auto& skybox = sky.Texture;
        const auto& state  = sky.State;
        using namespace RenderHardware;
        if (!skybox)
            return;
        auto desc = m_Renderer->ScenePipeline("skybox", SceneShaders::SkyboxVertex, SceneShaders::SkyboxFragment, true);
        if (!desc.Program)
            return;
        const auto& cube               = m_Renderer->GetCube();
        desc.VertexBindings            = {{0, 64, false}};
        desc.Attributes                = {{0, 0, 0, VertexFormat::Float3}};
        desc.DepthStencil.DepthCompare = state.DepthCompare;
        desc.DepthStencil.DepthWrite   = state.DepthWrite;
        desc.Blends[0].Enabled         = state.Blend;
        desc.Blends[0].WriteMask       = state.WriteMask;
        desc.Blends[0].SourceColor = desc.Blends[0].SourceAlpha = BlendFactor::SourceAlpha;
        desc.Blends[0].DestinationColor = desc.Blends[0].DestinationAlpha = BlendFactor::OneMinusSourceAlpha;
        desc.Blends[1]                                                    = desc.Blends[0];
        SceneDrawData data;
        data.DeviceClip     = DeviceProjection(glm::mat4(1), m_Renderer->GetDevice()->GetCoordinateConventions());
        data.ViewProjection = m_SceneInfo.sceneCamera.camera.GetProjectionMatrix();
        data.Transform      = m_SceneInfo.sceneCamera.viewMatrix;
        GetMaterialEnvironment();
        auto* commands = &commandList;
        commands->BeginRendering(SceneTarget(LoadOp::Load));
        commands->SetViewport({0, 0, float(m_ViewportWidth), float(m_ViewportHeight)});
        commands->BindPipeline(m_Renderer->CachedPipeline(desc));
        commands->BindGroup(DrawGroup(
            *m_Renderer->GetDevice(), desc.GroupLayouts[0], data, skybox, m_Renderer->GetMaterialEnvironmentSampler()));
        commands->BindVertexBuffers({{0, cube.Vertices, 0}});
        commands->BindIndexBuffer(cube.Indices, IndexType::Uint32);
        commands->DrawIndexed(cube.IndexCount);
        commands->EndRendering();
    }

} // namespace Kans
