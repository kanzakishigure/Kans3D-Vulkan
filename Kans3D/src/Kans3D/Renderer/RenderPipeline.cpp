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
        if (renderer.Samples > 1)
        {
            BuildMSAA(graph, renderer);
            return;
        }
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
        if (view.EffectiveAA == AntiAliasing::FXAA)
            AddFXAA(graph, renderer);
        Inject(RenderExtensionPoint::Overlay, featureContext);
    }

    void ForwardPipeline::AddFXAA(RenderGraph& graph, RenderFrame& frame)
    {
        using namespace RenderHardware;
        const auto output = graph.ImportTexture("SceneColor0", frame.Output.GetColors()[0]);
        auto       desc   = frame.Output.GetColors()[0]->GetDesc();
        desc.Usage        = RenderHardware::TextureUsage::Sampled | RenderHardware::TextureUsage::CopyDestination;
        const auto input  = graph.CreateTexture("FXAA LDR input", desc);
        auto       copy   = graph.AddPass(
            "FXAA input copy",
            {{output, GraphAccess::TransferRead}, {input, GraphAccess::TransferWrite}},
            [output, input](auto& c) { c.Commands.CopyTexture(c.Texture(output), {}, c.Texture(input)); });
        if (copy)
            graph.OrderAfter(copy, copy - 1);
        auto pass = graph.AddPass("FXAA quality (LDR)",
                                  {{input, GraphAccess::SampledRead}, {output, GraphAccess::ColorWrite}},
                                  [&frame, input, output](auto& c) {
                                      frame.Services.GetAntiAliasing().FXAA(
                                          c.Device, c.Commands, c.Texture(input), c.Texture(output));
                                  });
        graph.OrderAfter(pass, pass - 1);
    }
    void ForwardPipeline::BuildMSAA(RenderGraph& graph, RenderFrame& frame)
    {
        using namespace RenderHardware;
        auto& view       = frame.View;
        frame.SceneGraph = &graph;
        std::vector<GraphUse> scene, resolved;
        for (size_t i = 0; i < frame.Output.GetColors().size(); ++i)
        {
            auto desc     = frame.Output.GetColors()[i]->GetDesc();
            desc.Samples  = frame.Samples;
            const auto id = graph.CreateTexture("SceneColor MSAA " + std::to_string(i), desc);
            frame.MSColors.push_back(id);
            scene.push_back({id, GraphAccess::ColorReadWrite});
            auto out = graph.ImportTexture("SceneColor" + std::to_string(i), frame.Output.GetColors()[i]);
            graph.ExportTexture(out);
            resolved.push_back({out, GraphAccess::ColorReadWrite});
        }
        auto desc    = frame.Output.GetHDR()->GetDesc();
        desc.Samples = frame.Samples;
        frame.MSHDR  = graph.CreateTexture("SceneHDR MSAA", desc);
        scene.push_back({frame.MSHDR, GraphAccess::ColorReadWrite});
        desc          = frame.Output.GetDepth()->GetDesc();
        desc.Samples  = frame.Samples;
        frame.MSDepth = graph.CreateTexture("SceneDepthStencil MSAA", desc);
        scene.push_back({frame.MSDepth, GraphAccess::DepthReadWrite});
        const auto depth = graph.ImportTexture("SceneDepthStencil (sample 0 resolved)", frame.Output.GetDepth());
        graph.ExportTexture(depth);
        resolved.push_back({depth, GraphAccess::DepthReadWrite});
        auto append = [&](std::string name, std::vector<GraphUse> uses, RenderGraph::Record record) {
            const auto pass = graph.AddPass(std::move(name), std::move(uses), std::move(record));
            if (pass)
                graph.OrderAfter(pass, pass - 1);
        };
        auto clear = scene;
        for (auto& use : clear)
            use.Access = use.Texture == frame.MSDepth ? GraphAccess::DepthWrite : GraphAccess::ColorWrite;
        append("Clear MSAA", clear, [&frame](auto& c) {
            frame.DeferToneMapping = true;
            frame.PostProcessHDR   = false;
            c.Commands.BeginRendering(frame.SceneTarget(LoadOp::Clear));
            c.Commands.EndRendering();
            RenderingDesc hdr;
            hdr.Colors.push_back({frame.HDR(), {}, LoadOp::Clear});
            c.Commands.BeginRendering(hdr);
            c.Commands.EndRendering();
        });
        BuildShadowPasses(graph, frame);
        RenderFeatureContext features {graph, view, scene, &frame};
        for (const auto& immediate : view.ImmediateOrder)
        {
            features.Immediate = &immediate;
            Inject(RenderExtensionPoint::Immediate, features);
            auto uses = scene;
            if (immediate.Kind == ViewImmediateKind::Skybox)
            {
                const auto& sky = view.Skyboxes.at(immediate.Index);
                uses.push_back({graph.ImportTexture("Skybox", sky.Texture), GraphAccess::SampledRead});
                append("Skybox MSAA", uses, [&frame, immediate](auto& c) {
                    frame.DrawSkybox(frame.View.Skyboxes.at(immediate.Index), c.Commands);
                });
            }
            else if (immediate.Kind == ViewImmediateKind::Sprites)
            {
                const auto end = immediate.Count == SIZE_MAX ? view.Sprites.size() : immediate.Index + immediate.Count;
                for (size_t i = immediate.Index; i < end; ++i)
                    if (view.Sprites[i].Texture)
                        uses.push_back({graph.ImportTexture("SpriteTexture", view.Sprites[i].Texture->GetResource()),
                                        GraphAccess::SampledRead});
                // Deduplicate a shared image used by multiple sprites.
                std::sort(uses.begin(), uses.end(), [](auto a, auto b) { return a.Texture < b.Texture; });
                uses.erase(std::unique(uses.begin(), uses.end(), [](auto a, auto b) { return a.Texture == b.Texture; }),
                           uses.end());
                append("Renderer2D sprites MSAA", uses, [&frame, immediate, end](auto& c) {
                    if (frame.PostProcessHDR)
                    {
                        frame.Services.GetToneMapping().Record(
                            c.Device,
                            c.Commands,
                            frame.HDR(),
                            frame.Colors(),
                            frame.View.Exposure.value_or(frame.View.Scene.sceneCamera.camera.GetExposure()));
                        frame.PostProcessHDR = false;
                    }
                    auto& sprites = frame.Services.GetSpriteBatch();
                    sprites.ResetStats();
                    sprites.SetTarget(frame.GetDevice(), frame.SceneTarget(LoadOp::Load));
                    sprites.SetCommandList(&c.Commands);
                    sprites.BeginScene(frame.View.Scene.sceneCamera.camera, frame.View.Scene.sceneCamera.viewMatrix);
                    for (size_t i = immediate.Index; i < end; ++i)
                    {
                        const auto& s = frame.View.Sprites[i];
                        sprites.DrawQuad(s.Transform, s.Texture, s.Color);
                    }
                    sprites.EndScene();
                    frame.SpriteStats.DrawCalls += sprites.GetStats().DrawCalls;
                    frame.SpriteStats.QuadCount += sprites.GetStats().QuadCount;
                    sprites.ClearTarget();
                });
            }
            else
            {
                throw std::invalid_argument(
                    "MSAA views require sprite submissions, not pre-recorded single-sample batches");
            }
        }
        features.Immediate = nullptr;
        Inject(RenderExtensionPoint::BeforeOpaque, features);
        if (!view.MaterialOrder.empty())
        {
            auto uses    = scene;
            auto sampled = [&](const auto& image) {
                if (!image)
                    return;
                const auto id = graph.ImportTexture("MaterialTexture", image);
                if (std::none_of(uses.begin(), uses.end(), [&](auto u) { return u.Texture == id; }))
                    uses.push_back({id, GraphAccess::SampledRead});
            };
            for (const auto& image : frame.GetMaterialEnvironment())
                sampled(image);
            if (view.Shadows)
            {
                sampled(view.Shadows->Directional);
                for (const auto& image : view.Shadows->Points)
                    sampled(image);
            }
            for (const auto& draw : view.Draws)
                if (const auto* bindings = frame.GetBindings(draw.Material, draw.ViewMode != SceneViewMode::Lit))
                    for (const auto& image : bindings->Images)
                        sampled(image);
            append("Materials MSAA (opaque/mask, blend)", uses, [&frame](auto& c) {
                frame.DrawShaderMaterials(c.Commands);
            });
        }
        Inject(RenderExtensionPoint::AfterTransparent, features);
        const auto hdr         = graph.ImportTexture("SceneHDR (Karis resolved)", frame.Output.GetHDR());
        auto       resolveUses = scene;
        for (auto& use : resolveUses)
            use.Access = use.Texture == frame.MSDepth ? GraphAccess::DepthReadWrite :
                         use.Texture == frame.MSHDR   ? GraphAccess::ColorReadWrite :
                                                        GraphAccess::SampledRead;
        for (auto use : resolved)
        {
            use.Access = use.Texture == depth ? GraphAccess::DepthWrite : GraphAccess::ColorWrite;
            resolveUses.push_back(use);
        }
        resolveUses.push_back({hdr, GraphAccess::ColorWrite});
        append("MSAA Karis HDR + sample 0 depth/stencil resolve", resolveUses, [&frame](auto& c) {
            const float exposure = frame.View.Exposure.value_or(frame.View.Scene.sceneCamera.camera.GetExposure());
            if (!frame.PostProcessHDR)
                frame.Services.GetToneMapping().Record(
                    c.Device, c.Commands, frame.Colors()[0], {frame.HDR()}, exposure, true);
            frame.Services.GetAntiAliasing().Resolve(c.Device, c.Commands, frame.HDR(), frame.Output.GetHDR());
            // Compatibility color outputs and depth/stencil use attachment resolves.
            auto render = frame.SceneTarget(LoadOp::Load);
            for (size_t i = 0; i < render.Colors.size(); ++i)
                render.Colors[i].ResolveTarget = frame.Output.GetColors()[i];
            render.Depth.ResolveTarget = frame.Output.GetDepth();
            c.Commands.BeginRendering(render);
            c.Commands.EndRendering();
            frame.MSAAResolved   = true;
            frame.PostProcessHDR = true;
        });
        RenderFeatureContext postFeatures {graph, view, resolved, &frame};
        // Bloom consumes the single-sample HDR resolve. Its build-time contract is HDR input.
        frame.PostProcessHDR = true;
        Inject(RenderExtensionPoint::BeforePostProcess, postFeatures);
        auto toneUses = resolved;
        toneUses.pop_back();
        toneUses.push_back({hdr, GraphAccess::SampledRead});
        append("Tone mapping (ACES fitted + sRGB)", toneUses, [&frame](auto& c) {
            frame.Services.GetToneMapping().Record(
                c.Device,
                c.Commands,
                frame.Output.GetHDR(),
                frame.Output.GetColors(),
                frame.View.Exposure.value_or(frame.View.Scene.sceneCamera.camera.GetExposure()));
            frame.DeferToneMapping = frame.PostProcessHDR = false;
        });
        Inject(RenderExtensionPoint::Overlay, postFeatures);
    }
} // namespace Kans
