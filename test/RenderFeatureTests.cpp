#include <cstdlib>
#include <new>
#include <gtest/gtest.h>
#include "Kans3D/Renderer/Features/Editor/EditorRenderFeatures.h"
#include "Kans3D/Renderer/RenderFeature.h"

namespace
{
    thread_local bool   CountAllocations = false;
    thread_local size_t Allocations      = 0;
} // namespace
// This executable alone counts heap allocations during feature dispatch/toggling.
void* operator new(std::size_t size)
{
    if (CountAllocations)
        ++Allocations;
    if (auto* memory = std::malloc(size ? size : 1))
        return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void  operator delete(void* memory) noexcept { std::free(memory); }
void  operator delete[](void* memory) noexcept { std::free(memory); }
void  operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void  operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace
{
    using namespace Kans;
    using namespace Kans::RenderHardware;
    class CpuTexture final : public RenderHardware::Texture
    {
    public:
        explicit CpuTexture(TextureDesc desc) : Desc(desc) {}
        const TextureDesc& GetDesc() const override { return Desc; }
        TextureDesc        Desc;
    };
    TextureDesc Description()
    {
        TextureDesc desc;
        desc.Width = desc.Height = 16;
        desc.PixelFormat         = Format::R8Unorm;
        desc.Usage               = RenderHardware::TextureUsage::RenderTarget | RenderHardware::TextureUsage::Sampled;
        return desc;
    }
    std::vector<std::string> PassNames(const RenderGraph& graph)
    {
        std::vector<std::string> names;
        for (auto pass : graph.ExecutionOrder())
            names.push_back(graph.PassName(pass));
        return names;
    }
    class MarkerFeature final : public RenderFeature
    {
    public:
        explicit MarkerFeature(std::string name) : Name(std::move(name)) {}
        bool RunsAt(RenderExtensionPoint point) const override { return point == RenderExtensionPoint::Overlay; }
        void AddPasses(RenderExtensionPoint, const RenderFeatureContext& context) override
        {
            context.Graph.AddPass(Name + " first", {}, {}, true);
            context.Graph.AddPass(Name + " second", {}, {}, true);
        }
        std::string Name;
    };
    class TextureFeature final : public RenderFeature
    {
    public:
        explicit TextureFeature(bool exported) : Exported(exported) {}
        bool RunsAt(RenderExtensionPoint point) const override { return point == RenderExtensionPoint::BeforeOpaque; }
        void AddPasses(RenderExtensionPoint, const RenderFeatureContext& context) override
        {
            const auto source = context.Graph.CreateTexture("feature source", Description());
            const auto output = context.Graph.CreateTexture("feature output", Description());
            context.Graph.AddPass("feature producer", {{source, GraphAccess::ColorWrite}}, {});
            context.Graph.AddPass(
                "feature consumer", {{source, GraphAccess::SampledRead}, {output, GraphAccess::ColorWrite}}, {});
            if (Exported)
                context.Graph.ExportTexture(output);
        }
        bool Exported;
    };
    TEST(RenderFeatureCpu, SamePointKeepsRegistrationOrderAndRestoresPositionAfterToggle)
    {
        // Bug: dispatch through a name-sorted map, reverse feature iteration, or append
        // a re-enabled feature to the end; each changes this deliberately unsorted order.
        RenderFeatureSet features;
        features.Register("zeta", std::make_unique<MarkerFeature>("zeta"));
        features.Register("alpha", std::make_unique<MarkerFeature>("alpha"));
        RenderView                  view;
        const std::vector<GraphUse> target;
        for (int frame = 0; frame < 3; ++frame)
        {
            features.SetEnabled("zeta", frame != 1);
            RenderGraph          graph;
            RenderFeatureContext context {graph, view, target};
            features.Inject(RenderExtensionPoint::BeforeOpaque, context);
            EXPECT_EQ(graph.PassCount(), 0u);
            features.Inject(RenderExtensionPoint::Overlay, context);
            graph.Compile();
            EXPECT_EQ(PassNames(graph),
                      frame == 1 ?
                          (std::vector<std::string> {"alpha first", "alpha second"}) :
                          (std::vector<std::string> {"zeta first", "zeta second", "alpha first", "alpha second"}));
        }
    }
    class DelayedReaderFeature final : public RenderFeature
    {
    public:
        explicit DelayedReaderFeature(GraphTexture input) : Input(input) {}
        bool RunsAt(RenderExtensionPoint point) const override { return point == RenderExtensionPoint::BeforeOpaque; }
        void AddPasses(RenderExtensionPoint, const RenderFeatureContext& context) override
        {
            context.Graph.AddPass("delayed before", {{Input, GraphAccess::SampledRead}}, {}, true);
        }
        GraphTexture Input;
    };
    TEST(RenderFeatureCpu, PointOrderSurvivesAnEarlierFeatureWaitingForItsProducer)
    {
        // Bug: rely only on declaration-order ties; an earlier feature blocked on
        // a producer then executes after a ready feature at a later injection point.
        RenderGraph      graph;
        const auto       input = graph.CreateTexture("late supplied input", Description());
        RenderFeatureSet features;
        features.Register("reader", std::make_unique<DelayedReaderFeature>(input));
        features.Register("overlay", std::make_unique<MarkerFeature>("overlay"));
        RenderView                  view;
        const std::vector<GraphUse> target;
        RenderFeatureContext        context {graph, view, target};
        features.Inject(RenderExtensionPoint::BeforeOpaque, context);
        features.Inject(RenderExtensionPoint::Overlay, context);
        graph.AddPass("supply", {{input, GraphAccess::ColorWrite}}, {});
        graph.Compile();
        EXPECT_EQ(PassNames(graph),
                  (std::vector<std::string> {"supply", "delayed before", "overlay first", "overlay second"}));
    }
    TEST(RenderFeatureCpu, UnusedFeatureChainIsCulledAcrossLivePointBoundaries)
    {
        // Bug: enforce extension-point order with DependsOn, retaining dead scratch
        // producers as prerequisites of a later live feature and allocating their images.
        RenderFeatureSet features;
        features.Register("scratch", std::make_unique<TextureFeature>(false));
        features.Register("overlay", std::make_unique<MarkerFeature>("overlay"));
        RenderView                  view;
        RenderGraph                 graph;
        const std::vector<GraphUse> target;
        RenderFeatureContext        context {graph, view, target};
        features.Inject(RenderExtensionPoint::BeforeOpaque, context);
        features.Inject(RenderExtensionPoint::Overlay, context);
        graph.Compile();
        EXPECT_EQ(PassNames(graph), (std::vector<std::string> {"overlay first", "overlay second"}));
        ASSERT_EQ(graph.Resources().size(), 2u);
        for (const auto& resource : graph.Resources())
        {
            EXPECT_TRUE(resource.Accesses.empty());
            EXPECT_EQ(resource.PhysicalSlot, uint32_t(-1));
        }
        TransientTexturePool pool;
        size_t               allocations = 0;
        graph.Allocate(pool, [&](const auto& desc) {
            ++allocations;
            return std::make_shared<CpuTexture>(desc);
        });
        EXPECT_EQ(allocations, 0u);
        EXPECT_EQ(pool.Size(), 0u);
    }
    TEST(RenderFeatureCpu, LiveFeatureResourcesReuseStableFramesAndDisabledGap)
    {
        // Bug: give feature textures a fresh pool each frame, or flush that pool
        // when disabling the feature; both recreate the two overlapping resources.
        RenderFeatureSet features;
        features.Register("textures", std::make_unique<TextureFeature>(true));
        RenderView                      view;
        const std::vector<GraphUse>     target;
        TransientTexturePool            pool;
        size_t                          allocations = 0;
        Handle<RenderHardware::Texture> source, output;
        for (int frame = 0; frame < 24; ++frame)
        {
            features.SetEnabled("textures", frame != 12);
            RenderGraph graph;
            features.Inject(RenderExtensionPoint::BeforeOpaque, {graph, view, target});
            graph.Compile();
            graph.Allocate(pool, [&](const auto& desc) {
                ++allocations;
                return std::make_shared<CpuTexture>(desc);
            });
            if (frame == 12)
            {
                EXPECT_EQ(graph.PassCount(), 0u);
                EXPECT_TRUE(graph.Resources().empty());
            }
            else
            {
                EXPECT_EQ(PassNames(graph), (std::vector<std::string> {"feature producer", "feature consumer"}));
                ASSERT_EQ(graph.Resources().size(), 2u);
                if (frame == 0)
                {
                    source = graph.Texture(0);
                    output = graph.Texture(1);
                    ASSERT_TRUE(source);
                    ASSERT_TRUE(output);
                    EXPECT_NE(source, output);
                }
                EXPECT_EQ(graph.Texture(0), source);
                EXPECT_EQ(graph.Texture(1), output);
            }
            EXPECT_EQ(allocations, 2u);
            EXPECT_EQ(pool.Size(), 2u);
        }
    }
    struct Counters
    {
        size_t Queries = 0, Builds = 0;
    };
    class EmptyFeature final : public RenderFeature
    {
    public:
        explicit EmptyFeature(Counters& counters) : Counts(counters) {}
        bool RunsAt(RenderExtensionPoint point) const override
        {
            ++Counts.Queries;
            return point == RenderExtensionPoint::Overlay;
        }
        void      AddPasses(RenderExtensionPoint, const RenderFeatureContext&) override { ++Counts.Builds; }
        Counters& Counts;
    };
    TEST(RenderFeatureCpu, StableDispatchAndTogglesAllocateNothingAndSkipDisabledCallbacks)
    {
        // Bug: copy the registry, allocate an active-feature list, rebuild registration
        // when toggling, or consult/build a disabled feature before checking Enabled.
        RenderFeatureSet features;
        Counters         active, disabled;
        RegisterDefaultEditorFeatures(features);
        const std::string activeName   = "active feature with a long allocation-sensitive name";
        const std::string disabledName = "disabled feature with a long allocation-sensitive name";
        features.Register(activeName, std::make_unique<EmptyFeature>(active));
        features.Register(disabledName, std::make_unique<EmptyFeature>(disabled), false);
        RenderGraph                 graph;
        RenderView                  view;
        const std::vector<GraphUse> target;
        RenderFeatureContext        context {graph, view, target};
        Allocations      = 0;
        CountAllocations = true;
        for (int frame = 0; frame < 1000; ++frame)
        {
            features.SetEnabled(activeName, (frame % 2) == 0);
            for (auto point : {RenderExtensionPoint::Immediate,
                               RenderExtensionPoint::BeforeOpaque,
                               RenderExtensionPoint::AfterTransparent,
                               RenderExtensionPoint::BeforePostProcess,
                               RenderExtensionPoint::Overlay})
                features.Inject(point, context);
        }
        CountAllocations = false;
        EXPECT_EQ(Allocations, 0u);
        EXPECT_EQ(active.Queries, 2500u);
        EXPECT_EQ(active.Builds, 500u);
        EXPECT_EQ(disabled.Queries, 0u);
        EXPECT_EQ(disabled.Builds, 0u);
        EXPECT_EQ(graph.PassCount(), 0u);
    }
    TEST(RenderFeatureCpu, RejectsInvalidRegistrationsAndUnknownToggleNames)
    {
        // Bug: silently replace a registered feature, or silently ignore a typo in a toggle.
        RenderFeatureSet features;
        features.Register("marker", std::make_unique<MarkerFeature>("marker"));
        EXPECT_TRUE(features.IsEnabled("marker"));
        features.SetEnabled("marker", false);
        EXPECT_FALSE(features.IsEnabled("marker"));
        EXPECT_THROW(features.Register("marker", std::make_unique<MarkerFeature>("replacement")),
                     std::invalid_argument);
        EXPECT_THROW(features.Register("", std::make_unique<MarkerFeature>("empty")), std::invalid_argument);
        EXPECT_THROW(features.Register("null", {}), std::invalid_argument);
        EXPECT_THROW(features.SetEnabled("missing", true), std::invalid_argument);
        EXPECT_THROW(features.IsEnabled("missing"), std::invalid_argument);
    }
} // namespace
