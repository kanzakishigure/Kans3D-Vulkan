#include <array>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include "Kans3D/Asset/Runtime/StandardShader.h"
#include "Kans3D/Renderer/MaterialPreviewRenderer.h"
#include "Kans3D/Renderer/RenderFeature.h"
#include "Kans3D/Renderer/SceneRenderer.h"
#include "GpuTestContext.h"

namespace
{
    using namespace Kans;
    namespace RH = Kans::RenderHardware;
    class EmptyProvider final : public IAssetProvider
    {
    public:
        std::variant<std::vector<std::byte>, AssetError> Read(const ProductRecord&) const override
        {
            throw std::logic_error("The shared services tests only request built-in assets");
        }
    };
    class RendererServices : public testing::TestWithParam<GpuTestBackend>
    {
    protected:
        std::unique_ptr<GpuTestContext> Context;
        std::shared_ptr<RH::Device>     Device;
        std::shared_ptr<Kans::Renderer> Module;
        Ref<Scene>                      Logic;
        std::unique_ptr<RenderScene>    Scene;
        std::unique_ptr<AssetManager>   Assets;
        std::filesystem::path           Directory;
        void                            SetUp() override
        {
            Context = std::make_unique<GpuTestContext>(GetParam());
            ASSERT_NO_FATAL_FAILURE(Context->SetUp());
            Device = RH::GetSharedDevice();
            Module = Kans::Renderer::Acquire(Device);
            Logic  = CreateRef<Kans::Scene>();
            Scene  = std::make_unique<RenderScene>(Logic);
            Scene->OnViewportResize(16, 16);
            Assets = MakeManager();
            Directory =
                std::filesystem::temp_directory_path() / ("kans-services-" + SourceAssetID::Generate().ToString());
            std::filesystem::create_directory(Directory);
        }
        void TearDown() override
        {
            Module.reset();
            Device->WaitIdle();
            Assets.reset();
            Scene.reset();
            Logic.reset();
            Device.reset();
            Context.reset();
            std::filesystem::remove_all(Directory);
        }
        std::unique_ptr<AssetManager> MakeManager()
        {
            auto registry = AssetRegistry::FromProducts({});
            return std::make_unique<AssetManager>(
                CreateRef<EmptyProvider>(),
                CreateRef<const AssetRegistry>(std::get<AssetRegistry>(std::move(registry))));
        }
        std::unique_ptr<SceneRenderer> View()
        {
            auto view = std::make_unique<SceneRenderer>(Scene.get());
            view->SetAssetManager(Assets.get());
            return view;
        }
        std::filesystem::path EnvironmentFile(const char* name, uint8_t red)
        {
            auto          path = Directory / name;
            std::ofstream out(path, std::ios::binary);
            out << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 2\n";
            const std::array<uint8_t, 4> pixel {red, 80, 40, 129};
            for (int i = 0; i < 4; ++i)
                out.write(reinterpret_cast<const char*>(pixel.data()), pixel.size());
            return path;
        }
        std::vector<uint8_t> Bytes(const RH::Handle<RH::Texture>& texture, size_t bytes)
        {
            auto readback = Device->CreateBuffer({bytes, RH::BufferUsage::CopyDestination, RH::MemoryUsage::Readback});
            auto commands = Device->CreateCommandList();
            commands->CopyTextureToBuffer(texture, {}, readback);
            Device->Submit(commands);
            Device->WaitIdle();
            return readback->Read(0, bytes);
        }
    };
    TEST_P(RendererServices, ViewsAndPreviewReuseOneCacheAndOneEnvironmentPrecompute)
    {
        // Bug: each view/scene constructs its own cache or IBL, including preview's scene.
        RenderScene previewScene(Logic);
        previewScene.OnViewportResize(16, 16);
        auto first  = View();
        auto second = std::make_unique<SceneRenderer>(&previewScene);
        second->SetAssetManager(Assets.get());
        EXPECT_EQ(first->GetRenderer(), Module);
        EXPECT_EQ(second->GetRenderer(), Module);
        ASSERT_EQ(first->GetAssetCache(), second->GetAssetCache());
        const auto* shader = first->GetAssetCache()->GetShader(StandardShaderID());
        ASSERT_NE(shader, nullptr);
        EXPECT_EQ(second->GetAssetCache()->GetShader(StandardShaderID()), shader);
        first->SetAssetManager(Assets.get());
        EXPECT_EQ(first->GetAssetCache(), second->GetAssetCache());
        auto source = EnvironmentFile("shared.hdr", 100);
        Scene->PrepareEnvironment(source);
        previewScene.PrepareEnvironment(source.parent_path() / "." / source.filename());
        first->PrepareEnvironment({8, 2, 16, 8});
        ASSERT_TRUE(first->GetSkybox());
        EXPECT_EQ(Module->GetEnvironmentPrecomputeCount(), 1u);
        Device->WaitIdle();
        const auto before = Device->GetStats().LiveResources;
        second->PrepareEnvironment({8, 2, 16, 8});
        Device->WaitIdle();
        EXPECT_EQ(Device->GetStats().LiveResources, before);
        EXPECT_EQ(Module->GetEnvironmentPrecomputeCount(), 1u);
        EXPECT_EQ(first->GetSkybox(), second->GetSkybox());
        EXPECT_EQ(first->GetEnvironment().IrradianceMap, second->GetEnvironment().IrradianceMap);
        EXPECT_EQ(first->GetEnvironment().RadianceMap, second->GetEnvironment().RadianceMap);
        EXPECT_EQ(first->GetBrdfLUT(), second->GetBrdfLUT());
        MaterialPreviewRenderer preview(*second);
        MaterialPreviewOptions  options;
        options.Size = {16, 16};
        auto result  = preview.RenderImage(MaterialData {}, nullptr, options);
        ASSERT_TRUE(result.Target);
        EXPECT_TRUE(result.Complete);
        EXPECT_EQ(Module->GetEnvironmentPrecomputeCount(), 1u);
        EXPECT_EQ(second->GetAssetCache()->GetShader(StandardShaderID()), shader);
        EXPECT_EQ(first->GetMaterialEnvironment(), second->GetMaterialEnvironment());
        EXPECT_EQ(first->GetMaterialEnvironmentSampler(), second->GetMaterialEnvironmentSampler());
    }
    TEST_P(RendererServices, DifferentManagersNeverShareCachesAndDetachReleasesTheLastLease)
    {
        // Bug: device-wide cache ignores manager identity, or retains a dangling manager after detach.
        auto                            other  = MakeManager();
        auto                            first  = View();
        auto                            second = View();
        auto                            cache  = Module->AcquireAssetCache(*Assets);
        std::weak_ptr<RenderAssetCache> weak   = cache;
        Device->WaitIdle();
        const auto before = Device->GetStats().LiveResources;
        ASSERT_NE(cache->GetShader(StandardShaderID()), nullptr);
        EXPECT_GT(Device->GetStats().LiveResources, before);
        second->SetAssetManager(other.get());
        EXPECT_NE(first->GetAssetCache(), second->GetAssetCache());
        EXPECT_EQ(&first->GetAssetCache()->GetAssetManager(), Assets.get());
        EXPECT_EQ(&second->GetAssetCache()->GetAssetManager(), other.get());
        cache.reset();
        first->SetAssetManager(nullptr);
        EXPECT_EQ(first->GetAssetCache(), nullptr);
        EXPECT_TRUE(weak.expired());
        Device->WaitIdle();
        EXPECT_EQ(Device->GetStats().LiveResources, before);
        second->SetAssetManager(Assets.get());
        EXPECT_EQ(&second->GetAssetCache()->GetAssetManager(), Assets.get());
        second->SetAssetManager(nullptr);
    }
    TEST_P(RendererServices, WeakCacheObserversDoNotRetainModuleResourcesAfterLastDetach)
    {
        // Bug: the lease deleter retains its captured module until weak observers
        // die, keeping GPU resources/device alive after the final real consumer.
        Device->WaitIdle();
        const auto before = Device->GetStats().LiveResources;
        auto       cache  = Module->AcquireAssetCache(*Assets);
        ASSERT_NE(cache->GetShader(StandardShaderID()), nullptr);
        Module->GetCube();
        Module->GetMaterialEnvironment(EnvironmentLighting {});
        Device->WaitIdle();
        EXPECT_GT(Device->GetStats().LiveResources, before);
        std::weak_ptr<Kans::Renderer>   weakModule = Module;
        std::weak_ptr<RenderAssetCache> weakCache  = cache;
        Module.reset();
        ASSERT_FALSE(weakModule.expired()); // A live cache lease retains the service.
        EXPECT_EQ(cache->GetDevice(), Device);
        cache.reset();
        EXPECT_TRUE(weakCache.expired());
        EXPECT_TRUE(weakModule.expired()); // Weak observers retain no module/resources.
        Device->WaitIdle();
        EXPECT_EQ(Device->GetStats().LiveResources, before);
        Module = Kans::Renderer::Acquire(Device);
    }
    TEST_P(RendererServices, ViewResizeDoesNotReallocateAnotherViewOrPreview)
    {
        // Bug: module owns viewport textures or pool, so resizing one invalidates another.
        auto                    first  = View();
        auto                    second = View();
        MaterialPreviewRenderer preview(*second);
        MaterialPreviewOptions  options;
        options.Size = {16, 16};
        auto image   = preview.RenderImage(MaterialData {}, nullptr, options);
        ASSERT_TRUE(image.Target);
        auto            previewHDR   = preview.GetHDRTarget();
        const auto      previewBytes = Bytes(image.Target, 16 * 16 * 4);
        const auto      colors       = second->GetViewTarget().GetColors();
        const auto      hdr          = second->GetHDRTarget();
        const auto      depth        = second->GetRenderTarget().Depth.Target;
        const auto      output       = second->GetOutput(0);
        const auto      old          = first->GetHDRTarget();
        RH::TextureDesc desc;
        desc.Width = desc.Height = 16;
        auto& secondPool         = second->GetViewTarget().GetTransientPool();
        auto  pooled             = secondPool.Acquire({desc}, [&](const auto& d) { return Device->CreateTexture(d); });
        first->SetViewportSize(31, 7);
        EXPECT_NE(first->GetHDRTarget(), old);
        EXPECT_EQ(first->GetHDRTarget()->GetDesc().Width, 31u);
        EXPECT_EQ(first->GetHDRTarget()->GetDesc().Height, 7u);
        EXPECT_EQ(second->GetViewTarget().GetColors(), colors);
        EXPECT_EQ(second->GetHDRTarget(), hdr);
        EXPECT_EQ(second->GetRenderTarget().Depth.Target, depth);
        EXPECT_EQ(second->GetOutput(0), output);
        EXPECT_EQ(secondPool.Size(), 1u);
        EXPECT_EQ(secondPool.Acquire({desc},
                                     [&](const auto& d) {
                                         ADD_FAILURE();
                                         return Device->CreateTexture(d);
                                     }),
                  pooled);
        EXPECT_EQ(preview.GetHDRTarget(), previewHDR);
        EXPECT_EQ(Bytes(image.Target, 16 * 16 * 4), previewBytes);
    }
    TEST_P(RendererServices, SharedPickerUsesTheCallingViewsCurrentExtentAfterResize)
    {
        // Bug: shared picker remembers the last view's extent or reads IDs/depth from the old target.
        auto     first  = View();
        auto     second = View();
        MeshData data;
        data.Vertices.resize(3);
        data.Vertices[0].Position        = {-1, -1, 0};
        data.Vertices[1].Position        = {1, -1, 0};
        data.Vertices[2].Position        = {0, 1, 0};
        data.Indices                     = {0, 1, 2};
        auto                        mesh = std::get<GpuMesh>(UploadMesh(data, Device));
        const std::vector<MeshDraw> draws {{&mesh, glm::mat4(1), 0xf1234567u}};
        EXPECT_EQ(first->Pick(draws, glm::mat4(1), {8, 8}), 0xf1234567u);
        first->SetViewportSize(32, 8);
        auto hit = first->PickSurface(draws, glm::mat4(1), {16, 4});
        EXPECT_EQ(hit.ID, 0xf1234567u);
        ASSERT_TRUE(hit.Position);
        EXPECT_EQ(hit.Position->x, 0.03125f);
        EXPECT_EQ(hit.Position->y, 0.125f);
        EXPECT_EQ(hit.Position->z, 0.0f);
        EXPECT_EQ(second->Pick(draws, glm::mat4(1), {8, 8}), 0xf1234567u);
        auto miss              = draws;
        miss[0].Transform[3].x = 4.0f;
        EXPECT_EQ(first->Pick(miss, glm::mat4(1), {16, 4}), 0u);
        EXPECT_FALSE(first->PickSurface(miss, glm::mat4(1), {16, 4}).Position);
        EXPECT_EQ(second->Pick(miss, glm::mat4(1), {8, 8}), 0u);
    }
    TEST_P(RendererServices, EnvironmentChangesReachExistingViewsAndRefreshSharedResults)
    {
        // Bug: a view snapshots old environment handles forever, or refresh replaces only one lease.
        auto first  = View();
        auto second = View();
        auto path   = EnvironmentFile("original.hdr", 100);
        Scene->PrepareEnvironment(path);
        first->PrepareEnvironment({8, 2, 16, 8});
        second->PrepareEnvironment({8, 2, 16, 8});
        auto old     = second->GetSkybox();
        auto changed = EnvironmentFile("changed.hdr", 200);
        first->UpdateEnvironment(changed.string());
        ASSERT_TRUE(first->GetSkybox());
        EXPECT_NE(first->GetSkybox(), old);
        EXPECT_EQ(second->GetSkybox(), first->GetSkybox());
        EXPECT_EQ(Module->GetEnvironmentPrecomputeCount(), 2u);
        second->BeginScene({});
        second->EndScene();
        EXPECT_EQ(second->GetRenderView().EnvironmentMaps.IrradianceMap, first->GetEnvironment().IrradianceMap);
        EXPECT_EQ(second->GetRenderView().EnvironmentMaps.RadianceMap, first->GetEnvironment().RadianceMap);
        EXPECT_EQ(second->GetRenderView().BrdfLUT, first->GetBrdfLUT());
        old                 = second->GetSkybox();
        const auto oldBytes = Bytes(old, 8 * 8 * 8);
        EnvironmentFile("changed.hdr", 50);
        first->UpdateEnvironment(changed.string());
        EXPECT_NE(second->GetSkybox(), old);
        EXPECT_EQ(second->GetSkybox(), first->GetSkybox());
        EXPECT_NE(Bytes(second->GetSkybox(), 8 * 8 * 8), oldBytes);
        EXPECT_EQ(Module->GetEnvironmentPrecomputeCount(), 3u);
    }
    TEST_P(RendererServices, EnvironmentKeysNormalizePathsAndIncludeSizesAndRefreshEveryVariant)
    {
        // Bugs: path spelling duplicates IBL, omitted sizes aliases incompatible targets,
        // or source refresh leaves an already prepared resolution stale.
        auto path  = EnvironmentFile("variants.hdr", 100);
        auto small = Module->PrepareEnvironment(path, {8, 2, 16, 8});
        ASSERT_TRUE(small);
        auto alias = Module->PrepareEnvironment(path.parent_path() / "." / path.filename(), {8, 2, 16, 8});
        EXPECT_EQ(alias, small);
        EXPECT_EQ(Module->GetEnvironmentPrecomputeCount(), 1u);
        auto large = Module->PrepareEnvironment(path, {16, 4, 32, 16});
        ASSERT_TRUE(large);
        EXPECT_NE(large, small);
        EXPECT_NE(large->Skybox, small->Skybox);
        EXPECT_EQ(small->Skybox->GetDesc().Width, 8u);
        EXPECT_EQ(large->Skybox->GetDesc().Width, 16u);
        EXPECT_EQ(small->Maps.IrradianceMap->GetDesc().Width, 2u);
        EXPECT_EQ(large->Maps.IrradianceMap->GetDesc().Width, 4u);
        EXPECT_EQ(small->Maps.RadianceMap->GetDesc().Width, 16u);
        EXPECT_EQ(large->Maps.RadianceMap->GetDesc().Width, 32u);
        EXPECT_EQ(small->BrdfLUT->GetDesc().Width, 8u);
        EXPECT_EQ(large->BrdfLUT->GetDesc().Width, 16u);
        auto oldSmall = small->Skybox, oldLarge = large->Skybox;
        EnvironmentFile("variants.hdr", 200);
        EXPECT_EQ(Module->UpdateEnvironment(path, {8, 2, 16, 8}), small);
        EXPECT_NE(small->Skybox, oldSmall);
        EXPECT_NE(large->Skybox, oldLarge);
        EXPECT_EQ(Module->GetEnvironmentPrecomputeCount(), 4u);
        EXPECT_EQ(Module->PrepareEnvironment(path, {16, 4, 32, 16}), large);
        EXPECT_EQ(Module->GetEnvironmentPrecomputeCount(), 4u);
    }
    class PoolFeature final : public RenderFeature
    {
    public:
        bool RunsAt(RenderExtensionPoint point) const override { return point == RenderExtensionPoint::Overlay; }
        void AddPasses(RenderExtensionPoint, const RenderFeatureContext& context) override
        {
            RH::TextureDesc desc;
            desc.Width = desc.Height = 16;
            desc.Usage               = RH::TextureUsage::RenderTarget | RH::TextureUsage::Sampled;
            auto image               = context.Graph.CreateTexture("Service lifetime mask", desc);
            context.Graph.AddPass(
                "Service lifetime clear",
                {{image, GraphAccess::ColorWrite}},
                [image](auto& ctx) {
                    RH::RenderingDesc target;
                    target.Colors = {{ctx.Texture(image)}};
                    ctx.Commands.BeginRendering(target);
                    ctx.Commands.EndRendering();
                },
                true);
        }
    };
    TEST_P(RendererServices, DestroyedViewReleasesTargetsGraphAndTransientPoolWhileModuleStaysAlive)
    {
        // Bug: shared module retains a view target, graph command list or pool after view destruction.
        auto survivor = View();
        Device->WaitIdle();
        const auto                 before = Device->GetStats().LiveResources;
        std::weak_ptr<RH::Texture> color, hdr, depth, pooled;
        {
            auto doomed = View();
            color       = doomed->GetRenderTarget().Colors[0].Target;
            hdr         = doomed->GetHDRTarget();
            depth       = doomed->GetRenderTarget().Depth.Target;
            doomed->GetFeatures().Register("Lifetime", std::make_unique<PoolFeature>());
            doomed->BeginScene({});
            doomed->EndScene();
            const auto& graph = doomed->GetRenderGraph();
            ASSERT_EQ(doomed->GetViewTarget().GetTransientPool().Size(), 1u);
            for (uint32_t i = 0; i < graph.Resources().size(); ++i)
                if (graph.Resources()[i].Name == "Service lifetime mask")
                    pooled = graph.Texture(i);
            ASSERT_FALSE(pooled.expired());
        }
        Device->WaitIdle();
        EXPECT_TRUE(color.expired());
        EXPECT_TRUE(hdr.expired());
        EXPECT_TRUE(depth.expired());
        EXPECT_TRUE(pooled.expired());
        EXPECT_EQ(Device->GetStats().LiveResources, before);
        EXPECT_EQ(survivor->GetRenderer(), Module);
    }
    INSTANTIATE_TEST_SUITE_P(Backends,
                             RendererServices,
                             testing::Values(GpuTestBackend::OpenGL, GpuTestBackend::Vulkan),
                             [](const testing::TestParamInfo<GpuTestBackend>& info) {
                                 return info.param == GpuTestBackend::Vulkan ? "Vulkan" : "OpenGL";
                             });
} // namespace
