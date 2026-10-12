#pragma once
#include <functional>
#include <memory>
#include <string>
#include "Graph/RenderGraph.h"
#include "RenderView.h"

namespace Kans
{
    class SceneRenderer;
    class RenderPipeline
    {
    public:
        virtual ~RenderPipeline() = default;
        virtual void Begin(RenderGraph& graph, const RenderView& view, SceneRenderer& renderer) {}
        virtual void Build(RenderGraph& graph, const RenderView& view, SceneRenderer& renderer) = 0;
    };
    class ForwardPipeline final : public RenderPipeline
    {
    public:
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
