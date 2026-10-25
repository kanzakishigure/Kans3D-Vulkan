#include "kspch.h"
#include "EditorRenderFeatures.h"
#include "Kans3D/Renderer/SceneRenderer.h"

namespace Kans
{
    void RegisterDefaultEditorFeatures(RenderFeatureSet& features)
    {
        features.Register("EditorGrid", std::make_unique<EditorGridFeature>());
        features.Register("DebugGeometry", std::make_unique<DebugGeometryFeature>());
        features.Register("EditorSelection", std::make_unique<EditorSelectionFeature>());
        features.Register("EditorPreview", std::make_unique<EditorPreviewFeature>());
    }
    void EditorGridFeature::AddPasses(RenderExtensionPoint point, const RenderFeatureContext& context)
    {
        if (!context.Immediate || context.Immediate->Kind != ViewImmediateKind::Grid)
            return;
        auto&       renderer  = *context.Renderer;
        const auto& view      = context.View;
        const auto  immediate = *context.Immediate;
        context.Graph.AddPass("Grid", context.Target, [&renderer, &view, immediate](auto& pass) {
            renderer.DrawGrid(view.Grids.at(immediate.Index), pass.Commands);
        });
    }
    void DebugGeometryFeature::AddPasses(RenderExtensionPoint point, const RenderFeatureContext& context)
    {
        if (context.View.Geometry.empty())
            return;
        auto&       renderer = *context.Renderer;
        const auto& view     = context.View;
        context.Graph.AddPass("Legacy geometry", context.Target, [&renderer, &view](auto& pass) {
            for (const auto& draw : view.Geometry)
                renderer.DrawModelGeometry(draw, pass.Commands);
        });
    }
    void EditorOutlineFeature::AddOutline(const RenderFeatureContext& context,
                                          std::string                 name,
                                          bool                        preview,
                                          glm::vec4                   color)
    {
        using namespace RenderHardware;
        auto&       graph    = context.Graph;
        const auto& view     = context.View;
        auto&       renderer = *context.Renderer;
        const auto& target   = context.Target;
        const auto  depth    = target.back().Texture;
        TextureDesc desc;
        desc.Width       = view.Viewport.x;
        desc.Height      = view.Viewport.y;
        desc.PixelFormat = Format::R8Unorm;
        desc.Usage       = RenderHardware::TextureUsage::Sampled | RenderHardware::TextureUsage::RenderTarget;
        auto mask        = graph.CreateTexture(name + " mask", desc);
        graph.AddPass(
            name + " mask", {{mask, GraphAccess::ColorWrite}}, [&renderer, &view, preview, mask](auto& context) {
                // Resolve at consumption, preserving phase one's cache replacement safety.
                std::vector<MeshDraw> draws;
                if (preview)
                    renderer.AppendModelDraws(view.Preview->Model, view.Preview->Transform, 0, draws);
                else
                    for (const auto& model : view.Highlight)
                        renderer.AppendModelDraws(model.Model, model.Transform, 0, draws);
                renderer.m_Outline.RecordMask(context.Commands,
                                              context.Texture(mask),
                                              draws,
                                              view.Scene.sceneCamera.camera.GetProjectionMatrix() *
                                                  view.Scene.sceneCamera.viewMatrix,
                                              view.Viewport);
            });
        graph.AddPass(
            name + " composite",
            {{target[0].Texture, GraphAccess::ColorReadWrite},
             {depth, GraphAccess::DepthReadWrite},
             {mask, GraphAccess::SampledRead}},
            [&renderer, &view, mask, color](auto& context) {
                renderer.m_Outline.RecordComposite(
                    context.Commands, renderer.SceneTarget(LoadOp::Load), context.Texture(mask), view.Viewport, color);
            });
    }

    void EditorSelectionFeature::AddPasses(RenderExtensionPoint point, const RenderFeatureContext& context)
    {
        if (!context.View.Highlight.empty())
            AddOutline(context, "Selection", false, {1, .62f, .15f, 1});
    }
    void EditorPreviewFeature::Prepare(RenderView& view, SceneRenderer& renderer)
    {
        if (!view.Preview)
            return;
        // Keep the ghost in the original shared, sorted material batch, always lit.
        const SceneViewMode viewMode = renderer.m_ViewMode;
        renderer.m_ViewMode          = SceneViewMode::Lit;
        renderer.SubmitModel(view.Preview->Model, view.Preview->Transform);
        renderer.m_ViewMode = viewMode;
    }
    void EditorPreviewFeature::AddPasses(RenderExtensionPoint point, const RenderFeatureContext& context)
    {
        if (context.View.Preview)
            AddOutline(context, "Preview", true, {.35f, .7f, 1, 1});
    }
} // namespace Kans
