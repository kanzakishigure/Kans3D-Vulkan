#include "kspch.h"
#include "RenderPipeline.h"
#include <map>
#include <stdexcept>
#include "RenderFrame.h"
#include "Resource/InternalShader.h"
#include "Resource/ShadowMaps.h"
#include "SpriteBatch.h"

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
        if (context.View.Features)
            context.View.Features->Inject(point, context);
    }
    void ForwardPipeline::Prepare(RenderView& view)
    {
        using namespace RenderHardware;
        const auto projection =
            DeviceProjection(view.Scene.sceneCamera.camera.GetProjectionMatrix(),
                             {ClipYDirection::Down, DepthRange::ZeroToOne, FramebufferOrigin::UpperLeft});
        const Frustum frustum(projection * view.Scene.sceneCamera.viewMatrix);
        ExtractDrawLists(view, &frustum);
    }
    void ForwardPipeline::Begin(RenderGraph& graph, RenderFrame& renderer)
    {
        const auto& view = renderer.View;
        using namespace RenderHardware;
        std::vector<GraphUse> clear;
        for (size_t i = 0; i < renderer.Output.GetColors().size(); ++i)
        {
            auto image = graph.ImportTexture("SceneColor" + std::to_string(i), renderer.Output.GetColors()[i]);
            clear.push_back({image, GraphAccess::ColorWrite});
            graph.ExportTexture(image);
        }
        const auto depth = graph.ImportTexture("SceneDepthStencil", renderer.Output.GetDepth());
        clear.push_back({depth, GraphAccess::DepthWrite});
        graph.ExportTexture(depth);
        graph.AddPass("Clear", clear, [&renderer](auto& context) {
            context.Commands.BeginRendering(renderer.SceneTarget(LoadOp::Clear));
            context.Commands.EndRendering();
        });
    }
    void ForwardPipeline::Build(RenderGraph& graph, RenderFrame& renderer)
    {
        const auto& view = renderer.View;
        using namespace RenderHardware;
        if (!graph.PassCount())
            Begin(graph, renderer);
        BuildShadowPasses(graph, renderer);
        std::vector<GraphUse> target;
        for (size_t i = 0; i < renderer.Output.GetColors().size(); ++i)
            target.push_back({graph.ImportTexture("SceneColor" + std::to_string(i), renderer.Output.GetColors()[i]),
                              GraphAccess::ColorReadWrite});
        const auto depth = graph.ImportTexture("SceneDepthStencil", renderer.Output.GetDepth());
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
                auto& sprites = renderer.Services.GetSpriteBatch();
                sprites.ResetStats();
                sprites.SetTarget(renderer.GetDevice(), renderer.SceneTarget(LoadOp::Load));
                sprites.SetSubmissionSink([&](auto commands, auto image) {
                    auto batchUses = target;
                    sampled(batchUses, image, "SpriteTexture");
                    auto pass = graph.AddRecordedPass("Renderer2D sprites",
                                                      std::move(batchUses),
                                                      std::move(commands),
                                                      sprites.GetLastRecordMilliseconds());
                    if (pass)
                        graph.OrderAfter(pass, pass - 1);
                });
                sprites.BeginScene(view.Scene.sceneCamera.camera, view.Scene.sceneCamera.viewMatrix);
                const auto end = immediate.Count == SIZE_MAX ? view.Sprites.size() : immediate.Index + immediate.Count;
                for (size_t i = immediate.Index; i < end; ++i)
                {
                    const auto& sprite = view.Sprites.at(i);
                    sprites.DrawQuad(sprite.Transform, sprite.Texture, sprite.Color);
                }
                sprites.EndScene();
                renderer.SpriteStats.DrawCalls += sprites.GetStats().DrawCalls;
                renderer.SpriteStats.QuadCount += sprites.GetStats().QuadCount;
                sprites.ClearTarget();
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
            if (view.Shadows)
            {
                sampled(uses, view.Shadows->Directional, "DirectionalShadowAtlas");
                for (const auto& point : view.Shadows->Points)
                    sampled(uses, point, "PointShadowCube");
            }
            // Material groups resolve asynchronous textures while recording. Record once now,
            // declare the exact retained images, then let graph execution submit the immutable list.
            // This keeps the existing per-draw allocation count and avoids resolving groups twice.
            const auto hdr = graph.ImportTexture("SceneHDR", renderer.Output.GetHDR());
            uses.push_back({hdr, GraphAccess::ColorReadWrite});
            const auto          recordStart = RenderClockMilliseconds();
            auto                commands = renderer.GetDevice()->CreateCommandList();
            Handle<CommandList> post;
            renderer.DrawShaderMaterials(
                *commands, [&](const auto& image) { sampled(uses, image, "MaterialTexture"); }, {}, nullptr, &post);
            auto pass = graph.AddRecordedPass("Materials (opaque/mask, blend)",
                                              std::move(uses),
                                              commands,
                                              RenderClockMilliseconds() - recordStart -
                                                  renderer.FinalPostRecordMilliseconds.value_or(0));
            if (pass)
                graph.OrderAfter(pass, pass - 1);
            if (post)
            {
                auto postUses = target;
                postUses.pop_back(); // Fullscreen conversion does not access depth/stencil.
                postUses.push_back({hdr, GraphAccess::SampledRead});
                auto resolve = graph.AddRecordedPass("Tone mapping (ACES fitted + sRGB)",
                                                     std::move(postUses),
                                                     post,
                                                     renderer.FinalPostRecordMilliseconds);
                graph.OrderAfter(resolve, resolve - 1);
            }
        }
        Inject(RenderExtensionPoint::AfterTransparent, featureContext);
        Inject(RenderExtensionPoint::BeforePostProcess, featureContext);
        if (renderer.DeferToneMapping && renderer.PostProcessHDR)
        {
            auto uses = target;
            uses.pop_back(); // The final resolve does not access depth/stencil.
            uses.push_back({graph.ImportTexture("SceneHDR", renderer.Output.GetHDR()), GraphAccess::SampledRead});
            append("Tone mapping (ACES fitted + sRGB)", std::move(uses), [&renderer, &view](auto& context) {
                const float exposure = view.Exposure.value_or(view.Scene.sceneCamera.camera.GetExposure());
                renderer.Services.GetToneMapping().Record(
                    context.Device, context.Commands, renderer.Output.GetHDR(), renderer.Output.GetColors(), exposure);
            });
        }
        // Display-space overlays follow every post effect and the final tone mapping.
        renderer.DeferToneMapping = renderer.PostProcessHDR = false;
        Inject(RenderExtensionPoint::Overlay, featureContext);
    }
} // namespace Kans
