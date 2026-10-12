#include <gtest/gtest.h>
#include "Kans3D/Renderer/Graph/RenderGraph.h"

namespace
{
    using namespace Kans;
    using namespace Kans::RenderHardware;
    class CpuTexture final : public Texture
    {
    public:
        explicit CpuTexture(TextureDesc desc) : Desc(desc) {}
        const TextureDesc& GetDesc() const override { return Desc; }
        TextureDesc        Desc;
    };
    TextureDesc Description(uint32_t size = 16, Format format = Format::RGBA8Unorm)
    {
        TextureDesc desc;
        desc.Width = desc.Height = size;
        desc.PixelFormat         = format;
        desc.Usage               = TextureUsage::RenderTarget | TextureUsage::Sampled | TextureUsage::CopySource;
        return desc;
    }
    TEST(RenderGraphCpu, DependenciesReorderConsumersAndPublishAccessSequence)
    {
        // Catches declaration-order execution instead of producer dependencies.
        RenderGraph graph;
        auto        a = graph.CreateTexture("source", Description());
        auto        b = graph.CreateTexture("output", Description());
        auto consumer = graph.AddPass("consumer", {{a, GraphAccess::SampledRead}, {b, GraphAccess::ColorWrite}}, {});
        auto producer = graph.AddPass("producer", {{a, GraphAccess::ColorWrite}}, {});
        graph.ExportTexture(b);
        graph.Compile();
        EXPECT_EQ(graph.ExecutionOrder(), (std::vector<GraphPass> {producer, consumer}));
        auto& accesses = graph.Resources()[a].Accesses;
        ASSERT_EQ(accesses.size(), 2u);
        EXPECT_EQ(accesses[0].Pass, producer);
        EXPECT_EQ(accesses[0].ExecutionIndex, 0u);
        EXPECT_EQ(accesses[0].Access, GraphAccess::ColorWrite);
        EXPECT_EQ(accesses[1].Pass, consumer);
        EXPECT_EQ(accesses[1].ExecutionIndex, 1u);
        EXPECT_EQ(accesses[1].Access, GraphAccess::SampledRead);
    }
    TEST(RenderGraphCpu, CullsDeadChainsAndOverwritesButRetainsReadbackAndItsProducer)
    {
        // Catches keeping all writes to imports, or dropping side-effect producer chains.
        RenderGraph graph;
        auto        dead         = graph.CreateTexture("dead", Description());
        auto        intermediate = graph.CreateTexture("intermediate", Description());
        auto        output       = graph.ImportTexture("output", std::make_shared<CpuTexture>(Description()));
        graph.AddPass("unused", {{dead, GraphAccess::ColorWrite}}, {});
        graph.AddPass("unused consumer", {{dead, GraphAccess::SampledRead}}, {});
        graph.AddPass("overwritten", {{output, GraphAccess::ColorWrite}}, {});
        auto producer = graph.AddPass("producer", {{intermediate, GraphAccess::ColorWrite}}, {});
        auto readback = graph.AddPass("readback", {{intermediate, GraphAccess::TransferRead}}, {}, true);
        auto final    = graph.AddPass("final", {{output, GraphAccess::ColorWrite}}, {});
        graph.ExportTexture(output);
        graph.Compile();
        EXPECT_EQ(graph.ExecutionOrder(), (std::vector<GraphPass> {producer, readback, final}));
        EXPECT_TRUE(graph.Resources()[dead].Accesses.empty());
        EXPECT_EQ(graph.Resources()[dead].PhysicalSlot, uint32_t(-1));
    }
    TEST(RenderGraphCpu, WriteAfterReadMustFollowReaderEvenWhenWriterIsOtherwiseReady)
    {
        // Reader is delayed by another pass. Without WAR, overwrite runs before reader.
        RenderGraph graph;
        auto        image        = graph.ImportTexture("initial", std::make_shared<CpuTexture>(Description()));
        auto        reader       = graph.AddPass("read old contents", {{image, GraphAccess::SampledRead}}, {}, true);
        auto        writer       = graph.AddPass("overwrite", {{image, GraphAccess::ColorWrite}}, {});
        auto        prerequisite = graph.AddPass("delay reader", {}, {}, true);
        graph.DependsOn(reader, prerequisite);
        graph.ExportTexture(image);
        graph.Compile();
        EXPECT_EQ(graph.ExecutionOrder(), (std::vector<GraphPass> {prerequisite, reader, writer}));
    }
    TEST(RenderGraphCpu, ReadModifyWriteKeepsPreviousContentsAndOrdersMultipleReaders)
    {
        // Catches treating attachment Load+blend as a discard write, and losing one reader.
        RenderGraph graph;
        auto        image = graph.CreateTexture("image", Description());
        auto        clear = graph.AddPass("clear", {{image, GraphAccess::ColorWrite}}, {});
        auto        r1    = graph.AddPass("reader1", {{image, GraphAccess::SampledRead}}, {}, true);
        auto        r2    = graph.AddPass("reader2", {{image, GraphAccess::SampledRead}}, {}, true);
        auto        load  = graph.AddPass("blend", {{image, GraphAccess::ColorReadWrite}}, {});
        graph.DependsOn(r1, r2);
        graph.ExportTexture(image);
        graph.Compile();
        EXPECT_EQ(graph.ExecutionOrder(), (std::vector<GraphPass> {clear, r2, r1, load}));
    }
    TEST(RenderGraphCpu, AliasesOnlyStrictlyDisjointLifetimesAndCompatibleDescriptions)
    {
        // Catches <= at the lifetime boundary (source and output used in one pass alias).
        RenderGraph graph;
        auto        a     = graph.CreateTexture("a", Description());
        auto        b     = graph.CreateTexture("b", Description());
        auto        c     = graph.CreateTexture("c", Description());
        auto        other = graph.CreateTexture("other format", Description(16, Format::RGBA16Float));
        auto        p0    = graph.AddPass("a", {{a, GraphAccess::ColorWrite}}, {});
        auto        p1    = graph.AddPass("a to b", {{a, GraphAccess::SampledRead}, {b, GraphAccess::ColorWrite}}, {});
        auto        p2    = graph.AddPass("read b", {{b, GraphAccess::SampledRead}}, {}, true);
        auto        p3    = graph.AddPass("c", {{c, GraphAccess::ColorWrite}}, {});
        graph.DependsOn(p1, p0);
        graph.DependsOn(p2, p1);
        graph.DependsOn(p3, p2);
        graph.AddPass("read c", {{c, GraphAccess::SampledRead}}, {}, true);
        graph.AddPass("other", {{other, GraphAccess::ColorWrite}}, {}, true);
        graph.Compile();
        const auto& resources = graph.Resources();
        EXPECT_NE(resources[a].PhysicalSlot, resources[b].PhysicalSlot);
        EXPECT_EQ(resources[a].PhysicalSlot, resources[c].PhysicalSlot);
        EXPECT_NE(resources[c].PhysicalSlot, resources[other].PhysicalSlot);
        TransientTexturePool pool;
        size_t               allocations = 0;
        graph.Allocate(pool, [&](auto desc) {
            ++allocations;
            return std::make_shared<CpuTexture>(desc);
        });
        EXPECT_EQ(allocations, 3u);
        EXPECT_EQ(graph.Texture(a), graph.Texture(c));
        EXPECT_NE(graph.Texture(a), graph.Texture(b));
    }
    TEST(RenderGraphCpu, ExportedTextureCannotAliasBeforeOutputConsumption)
    {
        // Catches reusing a final output for a later scratch pass.
        RenderGraph graph;
        auto        a = graph.CreateTexture("output", Description());
        auto        b = graph.CreateTexture("scratch", Description());
        graph.AddPass("output", {{a, GraphAccess::ColorWrite}}, {});
        graph.AddPass("scratch", {{b, GraphAccess::ColorWrite}}, {}, true);
        graph.ExportTexture(a);
        graph.Compile();
        EXPECT_NE(graph.Resources()[a].PhysicalSlot, graph.Resources()[b].PhysicalSlot);
    }
    TEST(RenderGraphCpu, PoolReusesFramesAndReallocatesSizeFormatUsageMipAndDimension)
    {
        // Factory count independently checks reuse and every compatibility field.
        TransientTexturePool pool;
        size_t               allocations = 0;
        auto                 create      = [&](auto desc) {
            ++allocations;
            return std::make_shared<CpuTexture>(desc);
        };
        auto frame = [&](TextureDesc desc) {
            RenderGraph graph;
            auto        image = graph.CreateTexture("frame", desc);
            graph.AddPass("write", {{image, GraphAccess::ColorWrite}}, {}, true);
            graph.Compile();
            graph.Allocate(pool, create);
            return graph.Texture(image);
        };
        auto desc  = Description();
        auto first = frame(desc);
        for (int i = 0; i < 20; ++i)
            EXPECT_EQ(frame(desc), first);
        EXPECT_TRUE(pool.Acquire({}, create).empty());
        EXPECT_EQ(pool.Size(), 1u); // A frame without transients retains the reusable cache.
        EXPECT_EQ(frame(desc), first);
        EXPECT_EQ(allocations, 1u);
        for (int field = 0; field < 5; ++field)
        {
            if (field == 0)
                desc.Width = desc.Height = 32;
            if (field == 1)
                desc.PixelFormat = Format::RGBA16Float;
            if (field == 2)
                desc.Usage = desc.Usage | TextureUsage::CopyDestination;
            if (field == 3)
                desc.MipLevels = 2;
            if (field == 4)
                desc.Dimension = TextureDimension::Cube;
            auto next = frame(desc);
            EXPECT_NE(next, first);
            EXPECT_EQ(frame(desc), next);
            EXPECT_EQ(pool.Size(), 1u); // Old incompatible allocations do not accumulate.
            first = next;
            EXPECT_EQ(allocations, size_t(field + 2));
        }
    }
    TEST(RenderGraphCpu, RejectsCyclesEvenInDeadPassesAndExplainsInvalidReadsAndUsage)
    {
        // Dead-pass cycles must not disappear silently during culling.
        RenderGraph cycle;
        auto        a = cycle.AddPass("a", {}, {}), b = cycle.AddPass("b", {}, {});
        cycle.DependsOn(a, b);
        cycle.DependsOn(b, a);
        try
        {
            cycle.Compile();
            FAIL();
        }
        catch (const std::invalid_argument& e)
        {
            EXPECT_NE(std::string(e.what()).find("cycle"), std::string::npos);
        }
        RenderGraph uninitialized;
        auto        image = uninitialized.CreateTexture("uninitialized", Description());
        uninitialized.AddPass("load", {{image, GraphAccess::ColorReadWrite}}, {}, true);
        try
        {
            uninitialized.Compile();
            FAIL();
        }
        catch (const std::invalid_argument& e)
        {
            EXPECT_NE(std::string(e.what()).find("initialization"), std::string::npos);
            EXPECT_NE(std::string(e.what()).find("uninitialized"), std::string::npos);
        }
        RenderGraph bad;
        auto        desc = Description();
        desc.Usage       = TextureUsage::Sampled;
        image            = bad.CreateTexture("sample only", desc);
        bad.AddPass("wrong write", {{image, GraphAccess::ColorWrite}}, {}, true);
        EXPECT_THROW(bad.Compile(), std::invalid_argument);
        RenderGraph noProducer;
        image = noProducer.CreateTexture("missing producer", Description());
        noProducer.ExportTexture(image);
        EXPECT_THROW(noProducer.Compile(), std::invalid_argument);
        RenderGraph noWriter;
        image = noWriter.CreateTexture("read without writer", Description());
        noWriter.AddPass("read", {{image, GraphAccess::SampledRead}}, {}, true);
        EXPECT_THROW(noWriter.Compile(), std::invalid_argument);
        RenderGraph missing;
        missing.AddPass("missing", {{99, GraphAccess::SampledRead}}, {}, true);
        EXPECT_THROW(missing.Compile(), std::invalid_argument);
        RenderGraph duplicate;
        image = duplicate.CreateTexture("duplicate", Description());
        duplicate.AddPass("ambiguous", {{image, GraphAccess::ColorWrite}, {image, GraphAccess::SampledRead}}, {});
        EXPECT_THROW(duplicate.Compile(), std::invalid_argument);
    }
    TEST(RenderGraphCpu, ReimportingOneTexturePreservesLogicalIdentity)
    {
        // Duplicate imported handles must not bypass hazards/culling.
        RenderGraph graph;
        auto        texture = std::make_shared<CpuTexture>(Description());
        auto        a = graph.ImportTexture("a", texture), b = graph.ImportTexture("b", texture);
        EXPECT_EQ(a, b);
        auto writer = graph.AddPass("write", {{a, GraphAccess::ColorWrite}}, {});
        auto reader = graph.AddPass("read", {{b, GraphAccess::SampledRead}}, {}, true);
        graph.Compile();
        EXPECT_EQ(graph.ExecutionOrder(), (std::vector<GraphPass> {writer, reader}));
    }
} // namespace
