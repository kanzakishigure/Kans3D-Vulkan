#pragma once
#include <functional>
#include <memory>
#include <string>
#include "Graph/RenderGraph.h"
#include "RenderFeature.h"
#include "RenderView.h"

namespace Kans
{
    struct RenderFrame;
    class RenderPipeline
    {
    public:
        virtual ~RenderPipeline() = default;
        virtual void Prepare(RenderView& view) { ExtractDrawLists(view); }
        virtual void Begin(RenderGraph& graph, RenderFrame& frame) {}
        virtual void Build(RenderGraph& graph, RenderFrame& frame) = 0;

    protected:
        // Other pipelines use the same per-view features and extension-point contract.
        static void Inject(RenderExtensionPoint point, const RenderFeatureContext& context);
    };
    class ForwardPipeline final : public RenderPipeline
    {
    public:
        void Prepare(RenderView& view) override;
        void Begin(RenderGraph& graph, RenderFrame& frame) override;
        void Build(RenderGraph& graph, RenderFrame& frame) override;

    private:
        void BuildMSAA(RenderGraph& graph, RenderFrame& frame);
        void AddFXAA(RenderGraph& graph, RenderFrame& frame);
        void AddReconstructionFilter(RenderGraph& graph, RenderFrame& frame);
    };
    class RenderPipelineRegistry
    {
    public:
        using Factory = std::function<std::unique_ptr<RenderPipeline>()>;
        static void                            Register(std::string name, Factory factory);
        static std::unique_ptr<RenderPipeline> Create(const std::string& name);
    };
} // namespace Kans
