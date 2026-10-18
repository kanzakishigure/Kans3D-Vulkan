#include "kspch.h"
#include "RenderFrame.h"
#include "RenderPipeline.h"
#include "Resource/InternalShader.h"
#include "Resource/MaterialPass.h"
#include "Resource/SceneShaders.h"
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

    void RenderFrame::PrepareDrawLists()
    {
        for (auto& draw : View.Draws)
        {
            const auto* mesh     = GetMesh(draw.Mesh);
            const auto* bindings = GetBindings(draw.Material, draw.ViewMode != SceneViewMode::Lit);
            draw.Ready           = mesh && draw.Section < mesh->Sections.size() && bindings;
            if (draw.Ready)
            {
                draw.LocalBounds = mesh->Sections[draw.Section].LocalBounds;
                draw.Render      = bindings->Render;
            }
        }
    }

    bool RenderFrame::HasMaterialPass(const RenderView& view, const std::string& tag)
    {
        if (!Assets())
            return false;
        for (const auto& item : view.MaterialOrder)
        {
            const auto& draw = view.Draws[item.DrawIndex];
            if (!draw.Ready)
                continue;
            const auto* bindings = GetBindings(draw.Material, draw.ViewMode != SceneViewMode::Lit);
            const auto* shader   = bindings ? GetShader(draw.Material, bindings->Shader) : nullptr;
            if (shader && std::any_of(shader->Passes.begin(), shader->Passes.end(), [&](const auto& pass) {
                    return pass.Tag == tag;
                }))
                return true;
        }
        return false;
    }

    void RenderFrame::DrawShaderMaterials(
        RenderHardware::CommandList&                                                       commandList,
        const std::function<void(const RenderHardware::Handle<RenderHardware::Texture>&)>& sampled,
        const std::string&                                                                 tag,
        const RenderView*                                                                  suppliedView,
        RenderHardware::Handle<RenderHardware::CommandList>*                               finalPost)
    {
        const auto& view = suppliedView ? *suppliedView : View;
        if (!Assets() || view.MaterialOrder.empty())
            return;
        const auto&         ready    = view.MaterialOrder;
        auto*               commands = &commandList;
        EngineFrameUniforms frame    = View.LightingUniforms.value_or(MakeLightingUniforms(View.Scene));
        frame.ViewProjection = View.Scene.sceneCamera.camera.GetProjectionMatrix() * View.Scene.sceneCamera.viewMatrix;
        const auto coordinates = Services.GetDevice()->GetCoordinateConventions();
        frame.ViewProjection   = DeviceProjection(frame.ViewProjection, coordinates);
        frame.CameraPosition   = glm::vec4(View.Scene.sceneCamera.Position, 1.0f);
        frame.Time             = View.Time;
        const float rotation   = glm::radians(View.Environment.Rotation);
        frame.EnvironmentInfo  = {View.Environment.DiffuseIntensity,
                                  View.Environment.SpecularIntensity,
                                  std::cos(rotation),
                                  std::sin(rotation)};
        if (View.Exposure)
            frame.LightingInfo.y = *View.Exposure;
        using namespace RenderHardware;
        const size_t alignment   = Services.GetDevice()->GetBufferBindingAlignment(BindingType::UniformBuffer);
        const auto   align       = [alignment](size_t n) { return (n + alignment - 1) / alignment * alignment; };
        const size_t frameStride = align(sizeof(frame)), objectStride = align(sizeof(EngineObjectUniforms));
        // Each draw owns disjoint immutable ranges until command/frame retirement.
        if (ready.empty())
            return;
        auto frames =
            Services.GetDevice()->CreateBuffer({frameStride * ready.size(), BufferUsage::Uniform, MemoryUsage::Upload});
        auto objects = Services.GetDevice()->CreateBuffer(
            {objectStride * ready.size(), BufferUsage::Uniform, MemoryUsage::Upload});
        bool rendering = false, hdr = DeferToneMapping && PostProcessHDR;
        auto resolve = [&](RenderHardware::CommandList& output) {
            Services.GetToneMapping().Record(
                *Services.GetDevice(), output, Output.GetHDR(), Output.GetColors(), frame.LightingInfo.y);
        };
        auto beginTarget = [&](bool nextHDR) {
            if (rendering && nextHDR == hdr)
                return;
            if (rendering)
                commands->EndRendering();
            if (hdr && !nextHDR)
                resolve(*commands);
            if (nextHDR && !hdr)
                Services.GetToneMapping().Record(*Services.GetDevice(),
                                                 *commands,
                                                 Output.GetColors()[0],
                                                 {Output.GetHDR()},
                                                 frame.LightingInfo.y,
                                                 true);
            hdr = nextHDR;
            commands->BeginRendering(SceneTarget(LoadOp::Load, hdr));
            commands->SetViewport({0, 0, float(View.Viewport.x), float(View.Viewport.y), 0, 1});
            commands->SetScissor({0, 0, View.Viewport.x, View.Viewport.y});
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
            const auto* mesh     = GetMesh(draw.Mesh);
            const auto* bindings = GetBindings(draw.Material, draw.ViewMode != SceneViewMode::Lit);
            const auto* shader   = bindings ? GetShader(draw.Material, bindings->Shader) : nullptr;
            if (!mesh || draw.Section >= mesh->Sections.size() || !bindings || !shader)
                continue;
            if (!tag.empty() &&
                (!draw.Ready || std::none_of(shader->Passes.begin(), shader->Passes.end(), [&](const auto& pass) {
                    return pass.Tag == tag;
                })))
                continue;
            const auto& render = item.Render;
            frame              = litFrame;
            frame.DebugView    = draw.ViewMode == SceneViewMode::BaseColor      ? 1.0f :
                                 draw.ViewMode == SceneViewMode::Normals        ? 2.0f :
                                 draw.ViewMode == SceneViewMode::Wireframe      ? 3.0f :
                                 draw.ViewMode == SceneViewMode::ShadowCascades ? 4.0f :
                                                                                  0.0f;
            if (draw.ViewMode == SceneViewMode::Wireframe)
            {
                // The wireframe diagnostic intentionally retains its phase-one appearance.
                frame.LightingInfo.x = EngineLightCount;
                for (uint32_t i = 0; i < EngineLightCount; ++i)
                {
                    frame.PointPositions[i] = glm::vec4(View.Scene.pointLight.Position, 1);
                    frame.PointRadiance[i]  = glm::vec4(View.Scene.pointLight.Ambient_Intensity, 0);
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
            object.MaterialRender = {float(render.AlphaMode),
                                     render.AlphaCutoff,
                                     render.DoubleSided ? 1.0f : 0.0f,
                                     render.ReceiveShadows && draw.ReceiveShadows ? 0.0f : 1.0f};
            objects->Write(range * objectStride, &object, sizeof(object));
            MaterialPassData pass;
            pass.Frame              = frames;
            pass.Object             = objects;
            pass.FrameOffset        = frameRange * frameStride;
            pass.ObjectOffset       = range * objectStride;
            pass.Shadows            = View.Shadows;
            pass.Environment        = environment;
            pass.EnvironmentSampler = Services.GetMaterialEnvironmentSampler();
            // Debug views, including wireframe's lit shading, retain display output.
            const bool nextHDR =
                shader->OutputSpace == ShaderOutputSpace::LinearHDR && draw.ViewMode == SceneViewMode::Lit;
            beginTarget(nextHDR);
            pass.ColorFormats.clear();
            for (const auto& color : SceneTarget(LoadOp::Load, nextHDR).Colors)
                pass.ColorFormats.push_back(color.Target->GetDesc().PixelFormat);
            pass.Wireframe    = draw.ViewMode == SceneViewMode::Wireframe;
            pass.WriteStencil = draw.Stencil == ModelStencilMode::WriteReference;
            pass.Render       = render;
            pass.Section      = draw.Section;
            if (tag.empty())
                Complete &= RecordMaterialDraw(*Assets(), *commands, *shader, *bindings, *mesh, pass);
            else
                for (const auto& extra : shader->Passes)
                    if (extra.Tag == tag)
                    {
                        Complete &= RecordMaterialDraw(*Assets(), *commands, *shader, *bindings, *mesh, pass, &extra);
                        ++Stats.DrawCalls;
                        Stats.Triangles += mesh->Sections[draw.Section].IndexCount / 3;
                    }
            if (sampled)
                for (const auto& image : bindings->Images)
                    sampled(image);
            if (tag.empty())
            {
                ++Stats.DrawCalls;
                Stats.Triangles += mesh->Sections[draw.Section].IndexCount / 3;
            }
            ++range;
        }
        if (rendering)
            commands->EndRendering();
        if (tag.empty())
            for (auto index : DeferredGridAlpha)
                DrawGrid(View.Grids.at(index), *commands, GridChannels::Alpha, hdr);
        if (DeferToneMapping)
            PostProcessHDR = hdr;
        if (hdr && !DeferToneMapping)
        {
            if (finalPost)
            {
                *finalPost = Services.GetDevice()->CreateCommandList();
                resolve(**finalPost);
            }
            else
                resolve(*commands);
        }
    }

    void RenderFrame::DrawModelGeometry(const ViewGeometryDraw& draw, RenderHardware::CommandList& commandList)
    {
        using namespace RenderHardware;
        if (!Assets())
            return;
        ModelInstance instance;
        instance.Section     = draw.Section.value_or(0);
        instance.AllSections = !draw.Section;
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
        PipelineDesc desc = Services.ScenePipeline(draw.ShaderName.c_str(), vertex, fragment, false);
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
        auto  pipeline = Services.CachedPipeline(desc);
        auto* commands = &commandList;
        commands->BeginRendering(SceneTarget(LoadOp::Load));
        commands->SetViewport({0, 0, float(View.Viewport.x), float(View.Viewport.y)});
        commands->BindPipeline(pipeline);
        SceneDrawData data;
        data.DeviceClip     = DeviceProjection(glm::mat4(1), Services.GetDevice()->GetCoordinateConventions());
        data.ViewProjection = View.Scene.sceneCamera.camera.GetProjectionMatrix() * View.Scene.sceneCamera.viewMatrix;
        data.ViewPos        = glm::vec4(View.Scene.sceneCamera.Position, 1);
        if (const GpuMesh* mesh = GetMesh(draw.Mesh))
            ForEachSection(instance, *mesh, [&](const auto& part, const auto& section) {
                data.Transform = draw.Transform;
                commands->BindGroup(DrawGroup(*Services.GetDevice(), desc.GroupLayouts[0], data));
                commands->BindVertexBuffers({{0, normals ? mesh->NormalLines : mesh->Vertices, 0}});
                if (normals)
                    commands->Draw(section.IndexCount * 2, 1, section.FirstIndex * 2);
                else
                {
                    commands->BindIndexBuffer(mesh->Indices, IndexType::Uint32);
                    commands->DrawIndexed(section.IndexCount, 1, section.FirstIndex);
                }
                ++Stats.DrawCalls;
                Stats.Triangles += section.IndexCount / 3;
            });
        commands->EndRendering();
    }

    void RenderFrame::SubmitPrefabPreview(const PrefabPreview& preview)
    {
        if (!Assets())
            return;
        if (const auto* instances = Assets()->GetModel(preview.Prefab))
            for (const auto& instance : instances->Instances)
                if (const auto* mesh = GetMesh(instance.Mesh))
                    ForEachSection(instance, *mesh, [&](const auto& part, const auto&) {
                        ExtractMeshDraws(View,
                                         {{part.Mesh, {}, part.Section}, preview.Transform * part.Transform},
                                         SceneViewMode::Lit,
                                         mesh);
                    });
    }

    void RenderFrame::AppendPrefabPreviewDraws(const PrefabPreview& preview, std::vector<MeshDraw>& draws)
    {
        if (!Assets())
            return;
        if (const auto* instances = Assets()->GetModel(preview.Prefab))
            for (const auto& instance : instances->Instances)
                if (const auto* mesh = GetMesh(instance.Mesh))
                    ForEachSection(instance, *mesh, [&](const auto& part, const auto&) {
                        draws.push_back({mesh, preview.Transform * part.Transform, 0, part.Section});
                    });
    }

    void RenderFrame::DrawGrid(const ViewGrid&              grid,
                               RenderHardware::CommandList& commandList,
                               GridChannels                 channels,
                               bool                         hdr)
    {
        const auto  size = grid.Size, scale = grid.Scale;
        const auto& transform = grid.Transform;
        const auto& state     = grid.State;
        using namespace RenderHardware;
        auto desc = Services.ScenePipeline("GridShader", SceneShaders::GridVertex, SceneShaders::GridFragment, false);
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
        if (channels == GridChannels::Color)
            desc.Blends[0].WriteMask &= 7;
        else if (channels == GridChannels::Alpha)
        {
            desc.Blends[0].WriteMask &= 8;
            desc.Blends[1].WriteMask     = 0;
            desc.DepthStencil.DepthWrite = false;
        }
        if (hdr)
            desc.ColorFormats[0] = Output.GetHDR()->GetDesc().PixelFormat;
        const float    vertices[]       = {-0.5f, 0, -0.5f, 0, 0, 0.5f,  0, -0.5f, 1, 0,
                                           0.5f,  0, 0.5f,  1, 1, -0.5f, 0, 0.5f,  0, 1};
        const uint32_t indices[]        = {0, 1, 2, 2, 3, 0};
        auto vb = Services.GetDevice()->CreateBuffer({sizeof(vertices), BufferUsage::Vertex, MemoryUsage::Upload});
        auto ib = Services.GetDevice()->CreateBuffer({sizeof(indices), BufferUsage::Index, MemoryUsage::Upload});
        vb->Write(0, vertices, sizeof(vertices));
        ib->Write(0, indices, sizeof(indices));
        SceneDrawData data;
        data.DeviceClip     = DeviceProjection(glm::mat4(1), Services.GetDevice()->GetCoordinateConventions());
        data.ViewProjection = View.Scene.sceneCamera.camera.GetProjectionMatrix() * View.Scene.sceneCamera.viewMatrix;
        data.Transform      = transform;
        data.Settings       = {scale, size, 0, 0};
        auto* commands      = &commandList;
        commands->BeginRendering(SceneTarget(LoadOp::Load, hdr));
        commands->SetViewport({0, 0, float(View.Viewport.x), float(View.Viewport.y)});
        commands->BindPipeline(Services.CachedPipeline(desc));
        commands->BindGroup(DrawGroup(*Services.GetDevice(), desc.GroupLayouts[0], data));
        commands->BindVertexBuffers({{0, vb, 0}});
        commands->BindIndexBuffer(ib, IndexType::Uint32);
        commands->DrawIndexed(6);
        commands->EndRendering();
    }

    void RenderFrame::DrawSkybox(const ViewSkybox& sky, RenderHardware::CommandList& commandList)
    {
        const auto& skybox = sky.Texture;
        const auto& state  = sky.State;
        using namespace RenderHardware;
        if (!skybox || !View.Environment.ShowSkybox)
            return;
        auto desc = Services.ScenePipeline("skybox", SceneShaders::SkyboxVertex, SceneShaders::SkyboxFragment, true);
        if (!desc.Program)
            return;
        const auto& cube               = Services.GetCube();
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
        data.DeviceClip     = DeviceProjection(glm::mat4(1), Services.GetDevice()->GetCoordinateConventions());
        data.ViewProjection = View.Scene.sceneCamera.camera.GetProjectionMatrix();
        data.Transform      = View.Scene.sceneCamera.viewMatrix;
        const float rotation = glm::radians(View.Environment.Rotation);
        data.Settings        = {View.Environment.SkyboxIntensity, std::cos(rotation), std::sin(rotation), 0};
        GetMaterialEnvironment();
        auto* commands = &commandList;
        commands->BeginRendering(SceneTarget(LoadOp::Load));
        commands->SetViewport({0, 0, float(View.Viewport.x), float(View.Viewport.y)});
        commands->BindPipeline(Services.CachedPipeline(desc));
        commands->BindGroup(DrawGroup(
            *Services.GetDevice(), desc.GroupLayouts[0], data, skybox, Services.GetMaterialEnvironmentSampler()));
        commands->BindVertexBuffers({{0, cube.Vertices, 0}});
        commands->BindIndexBuffer(cube.Indices, IndexType::Uint32);
        commands->DrawIndexed(cube.IndexCount);
        commands->EndRendering();
    }

    std::array<RenderHardware::Handle<RenderHardware::Texture>, 3> RenderFrame::GetMaterialEnvironment()
    {
        EnvironmentLighting lighting;
        lighting.Maps.IrradianceMap = View.EnvironmentMaps.IrradianceMap;
        lighting.Maps.RadianceMap   = View.EnvironmentMaps.RadianceMap;
        lighting.BrdfLUT            = View.BrdfLUT;
        return Services.GetMaterialEnvironment(lighting);
    }
    void RenderFrame::AppendEntityDraws(UUID entity, uint32_t id, std::vector<MeshDraw>& draws)
    {
        Services.AppendEntityDraws(World, View, entity, id, draws);
    }
    const GpuMesh* RenderFrame::GetMesh(const MeshKey& id) const
    {
        auto it = View.MeshResources.find(id);
        return it != View.MeshResources.end() ? it->second.get() : Assets() ? Assets()->GetMesh(id) : nullptr;
    }
    const GpuMaterialBindings* RenderFrame::GetBindings(AssetID id, bool debug) const
    {
        auto it = View.MaterialResources.find(id);
        return it != View.MaterialResources.end() ? it->second.Bindings.get() :
               Assets()                           ? Assets()->GetMaterialBindings(id, debug) :
                                                    nullptr;
    }
    const GpuShader* RenderFrame::GetShader(AssetID material, AssetID shader) const
    {
        auto it = View.MaterialResources.find(material);
        return it != View.MaterialResources.end() ? it->second.Shader.get() :
               Assets()                           ? Assets()->GetShader(shader) :
                                                    nullptr;
    }
} // namespace Kans
