#include "kspch.h"
#include "RenderPipeline.h"
#include <map>
#include <stdexcept>
#include "Renderer2D.h"
#include "SceneRenderer.h"

namespace Kans
{
    namespace
    {
        std::map<std::string, RenderPipelineRegistry::Factory>& Factories()
        {
            static std::map<std::string, RenderPipelineRegistry::Factory> factories {
                {"Forward", [] { return std::make_unique<ForwardPipeline>(); }}};
            return factories;
        }
    } // namespace
    void RenderPipelineRegistry::Register(std::string name, Factory factory)
    {
        if (name.empty() || !factory)
            throw std::invalid_argument("RenderPipeline: invalid registration");
        if (!Factories().emplace(std::move(name), std::move(factory)).second)
            throw std::invalid_argument("RenderPipeline: duplicate registration");
    }
    std::unique_ptr<RenderPipeline> RenderPipelineRegistry::Create(const std::string& name)
    {
        auto it = Factories().find(name);
        if (it == Factories().end())
            throw std::invalid_argument("RenderPipeline: unknown pipeline '" + name + "'");
        auto pipeline = it->second();
        if (!pipeline)
            throw std::invalid_argument("RenderPipeline: factory returned null");
        return pipeline;
    }
    void RenderPipeline::Inject(RenderExtensionPoint point, const RenderFeatureContext& context)
    {
        context.Renderer->GetFeatures().Inject(point, context);
    }
    void ForwardPipeline::Begin(RenderGraph& graph, const RenderView& view, SceneRenderer& renderer)
    {
        using namespace RenderHardware;
        std::vector<GraphUse> clear;
        for (size_t i = 0; i < renderer.m_Target->GetColors().size(); ++i)
        {
            auto image = graph.ImportTexture("SceneColor" + std::to_string(i), renderer.m_Target->GetColors()[i]);
            clear.push_back({image, GraphAccess::ColorWrite});
            graph.ExportTexture(image);
        }
        const auto depth = graph.ImportTexture("SceneDepthStencil", renderer.m_Target->GetDepth());
        clear.push_back({depth, GraphAccess::DepthWrite});
        graph.ExportTexture(depth);
        graph.AddPass("Clear", clear, [&renderer](auto& context) {
            context.Commands.BeginRendering(renderer.SceneTarget(LoadOp::Clear));
            context.Commands.EndRendering();
        });
    }
    void ForwardPipeline::Build(RenderGraph& graph, const RenderView& view, SceneRenderer& renderer)
    {
        using namespace RenderHardware;
        if (!graph.PassCount())
            Begin(graph, view, renderer);
        std::vector<GraphUse> target;
        for (size_t i = 0; i < renderer.m_Target->GetColors().size(); ++i)
            target.push_back({graph.ImportTexture("SceneColor" + std::to_string(i), renderer.m_Target->GetColors()[i]),
                              GraphAccess::ColorReadWrite});
        const auto depth = graph.ImportTexture("SceneDepthStencil", renderer.m_Target->GetDepth());
        target.push_back({depth, GraphAccess::DepthReadWrite});
        // Preserve deterministic legacy order, even when two passes have disjoint writes.
        auto append = [&](std::string name, std::vector<GraphUse> uses, RenderGraph::Record record) {
            auto pass = graph.AddPass(std::move(name), std::move(uses), std::move(record));
            if (pass)
                graph.OrderAfter(pass, pass - 1);
            return pass;
        };
        auto sampled =
            [&](std::vector<GraphUse>& uses, const Handle<RenderHardware::Texture>& image, std::string name) {
                if (!image)
                    return;
                auto texture = graph.ImportTexture(std::move(name), image);
                if (std::none_of(uses.begin(), uses.end(), [&](auto use) { return use.Texture == texture; }))
                    uses.push_back({texture, GraphAccess::SampledRead});
            };
        RenderFeatureContext featureContext {graph, view, target, &renderer};
        for (const auto& immediate : view.ImmediateOrder)
        {
            featureContext.Immediate = &immediate;
            Inject(RenderExtensionPoint::Immediate, featureContext);
            if (immediate.Kind == ViewImmediateKind::Grid)
                continue; // This intent is consumed by the registered immediate features.
            auto uses = target;
            if (immediate.Kind == ViewImmediateKind::RecordedSprites)
            {
                const auto& batch = view.SpriteBatches.at(immediate.Index);
                sampled(uses, batch.Texture, "SpriteTexture");
                auto pass = graph.AddRecordedPass("Renderer2D batch", std::move(uses), batch.Commands);
                if (pass)
                    graph.OrderAfter(pass, pass - 1);
            }
            else if (immediate.Kind == ViewImmediateKind::Sprites)
            {
                // Flush reports the actual image, including the internal white fallback.
                // Record each original batch once and declare its exact sampled resource.
                Renderer2D::ResetStats();
                Renderer2D::SetSubmissionSink([&](auto commands, auto image) {
                    auto batchUses = target;
                    sampled(batchUses, image, "SpriteTexture");
                    auto pass = graph.AddRecordedPass("Renderer2D sprites", std::move(batchUses), std::move(commands));
                    if (pass)
                        graph.OrderAfter(pass, pass - 1);
                });
                Renderer2D::BeginScene(view.Scene.sceneCamera.camera, view.Scene.sceneCamera.viewMatrix);
                for (const auto& sprite : view.Sprites)
                    Renderer2D::DrawQuad(sprite.Transform, sprite.Texture, sprite.Color);
                Renderer2D::EndScene();
                Renderer2D::SetSubmissionSink({});
            }
            else
            {
                sampled(uses, view.Skyboxes.at(immediate.Index).Texture, "Skybox");
                append("Skybox", std::move(uses), [&renderer, &view, immediate](auto& context) {
                    renderer.DrawSkybox(view.Skyboxes.at(immediate.Index), context.Commands);
                });
            }
        }
        featureContext.Immediate = nullptr;
        Inject(RenderExtensionPoint::BeforeOpaque, featureContext);
        if (!view.MaterialOrder.empty())
        {
            auto uses = target;
            for (const auto& image : renderer.GetMaterialEnvironment())
                sampled(uses, image, "MaterialEnvironment");
            // Material groups resolve asynchronous textures while recording. Record once now,
            // declare the exact retained images, then let graph execution submit the immutable list.
            // This keeps the existing per-draw allocation count and avoids resolving groups twice.
            const auto hdr = graph.ImportTexture("SceneHDR", renderer.m_Target->GetHDR());
            uses.push_back({hdr, GraphAccess::ColorReadWrite});
            auto                commands = renderer.m_Renderer->GetDevice()->CreateCommandList();
            Handle<CommandList> post;
            renderer.DrawShaderMaterials(
                *commands, [&](const auto& image) { sampled(uses, image, "MaterialTexture"); }, {}, nullptr, &post);
            auto pass = graph.AddRecordedPass("Materials (opaque/mask, blend)", std::move(uses), commands);
            if (pass)
                graph.OrderAfter(pass, pass - 1);
            if (post)
            {
                auto postUses = target;
                postUses.pop_back(); // Fullscreen conversion does not access depth/stencil.
                postUses.push_back({hdr, GraphAccess::SampledRead});
                auto resolve = graph.AddRecordedPass("Tone mapping (ACES fitted + sRGB)", std::move(postUses), post);
                graph.OrderAfter(resolve, resolve - 1);
            }
        }
        Inject(RenderExtensionPoint::AfterTransparent, featureContext);
        Inject(RenderExtensionPoint::BeforePostProcess, featureContext);
        Inject(RenderExtensionPoint::Overlay, featureContext);
    }
} // namespace Kans
