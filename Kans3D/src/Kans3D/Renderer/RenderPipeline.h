#pragma once
#include <functional>
#include <memory>
#include <string>
#include "Graph/RenderGraph.h"
#include "RenderFeature.h"
#include "RenderView.h"

namespace Kans
{
    class SceneRenderer;
    class RenderPipeline
    {
    public:
        virtual ~RenderPipeline() = default;
        virtual void Prepare(RenderView& view) { ExtractDrawLists(view); }
        virtual void Begin(RenderGraph& graph, const RenderView& view, SceneRenderer& renderer) {}
        virtual void Build(RenderGraph& graph, const RenderView& view, SceneRenderer& renderer) = 0;

    protected:
        // Other pipelines use the same per-view features and extension-point contract.
        static void Inject(RenderExtensionPoint point, const RenderFeatureContext& context);
    };
    class ForwardPipeline final : public RenderPipeline
    {
    public:
        void Prepare(RenderView& view) override;
        void Begin(RenderGraph& graph, const RenderView& view, SceneRenderer& renderer) override;
        void Build(RenderGraph& graph, const RenderView& view, SceneRenderer& renderer) override;
    };
    class RenderPipelineRegistry
    {
    public:
        using Factory = std::function<std::unique_ptr<RenderPipeline>()>;
        static void                            Register(std::string name, Factory factory);
        static std::unique_ptr<RenderPipeline> Create(const std::string& name);
    };
} // namespace Kans
