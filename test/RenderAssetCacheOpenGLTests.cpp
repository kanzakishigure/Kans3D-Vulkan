#include <glad/glad.h>
#include <imgui.h>
#include <chrono>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>
#include <spdlog/sinks/ostream_sink.h>
#include "Kans3D/Asset/Importer/MaterialSource.h"
#include "Kans3D/Asset/Product/ContentDigest.h"
#include "Kans3D/Asset/Product/MeshProduct.h"
#include "Kans3D/Asset/Product/ModelProduct.h"
#include "Kans3D/Asset/Product/ShaderProduct.h"
#include "Kans3D/Asset/Product/TextureProduct.h"
#include "Kans3D/Asset/Runtime/StandardShader.h"
#include "Kans3D/Core/Log/Log.h"
#include "Kans3D/Project/Project.h"
#include "Kans3D/RHI/Backend.h"
#include "Kans3D/RHI/ImGuiRenderer.h"
#include "Kans3D/RHI/OpenGL/OpenGLTestProbes.h"
#include "Kans3D/RHI/Vulkan/VulkanTestProbes.h"
#include "Kans3D/Renderer/Features/MaterialPassFeature.h"
#include "Kans3D/Renderer/MaterialPreviewRenderer.h"
#include "Kans3D/Renderer/Resource/InternalShader.h"
#include "Kans3D/Renderer/Resource/RenderAssetCache.h"
#include "Kans3D/Renderer/Resource/SelectionTools.h"
#include "Kans3D/Renderer/SceneRenderer.h"
#include "Kans3D/Renderer/Features/Editor/EditorRenderFeatures.h"
#include "Kans3D/Scene/Entity.h"
#include "GpuTestContext.h"
#include "MaterialShaderTestUtilities.h"
#include "MultiPassTestUtilities.h"

namespace
{
    using namespace Kans;
    using namespace std::chrono_literals;

    // Native probes and immediate draws in these legacy GL tests live here rather
    // than requiring preview-only binding methods on the engine's RHI owners.
    namespace NativeProbe = Kans::RenderHardware::OpenGLTestProbes;
    uint32_t NativeProgram(const GpuShader& shader) { return NativeProbe::ProgramName(shader.Program); }
    uint32_t NativeBuffer(const GpuUniformBuffer& buffer) { return NativeProbe::BufferName(buffer.Resource()); }
    void     NativeBind(const GpuShader& shader) { glUseProgram(NativeProgram(shader)); }
    void     NativeBind(const GpuUniformBuffer& buffer, uint32_t point)
    {
        glBindBufferBase(GL_UNIFORM_BUFFER, point, NativeBuffer(buffer));
    }
    uint32_t MaterialBlockPoint(const GpuShader& shader, uint32_t binding)
    {
        return NativeProbe::UniformPoint(shader.Program, 1, binding);
    }
    uint32_t MaterialSamplerUnit(const GpuShader& shader, uint32_t binding)
    {
        return NativeProbe::TextureUnit(shader.Program, 1, binding);
    }
    bool BindMaterialTextures(RenderAssetCache& cache, const GpuShader& shader, const GpuMaterialBindings& bindings)
    {
        bool complete = true;
        cache.GetMaterialGroup(shader, bindings, &complete);
        size_t i = 0;
        for (const auto& [binding, role] : shader.MaterialSamplerFallbacks())
            NativeProbe::BindTexture(bindings.Images.at(i++), MaterialSamplerUnit(shader, binding));
        return complete;
    }
    struct NativeMeshArray
    {
        RenderHardware::Handle<RenderHardware::Pipeline> Pipeline;
        RenderHardware::Handle<RenderHardware::Buffer>   Vertices, Indices;
        explicit NativeMeshArray(const GpuMesh& mesh) : Vertices(mesh.Vertices), Indices(mesh.Indices)
        {
            auto program = CreateInternalShader(
                *mesh.Device,
                "Test mesh layout",
                "#version 450\nlayout(location=0) in vec3 Position; void main(){gl_Position=vec4(Position,1);}",
                "#version 450\nlayout(location=0) out vec4 Color; void main(){Color=vec4(1);}");
            RenderHardware::PipelineDesc desc;
            desc.Program        = std::get<RenderHardware::Handle<RenderHardware::Shader>>(program);
            desc.VertexBindings = mesh.VertexBindings;
            desc.Attributes     = mesh.Attributes;
            desc.ColorFormats   = {RenderHardware::Format::RGBA8Unorm};
            desc.Blends.resize(1);
            Pipeline = mesh.Device->CreatePipeline(desc);
        }
        void Bind() const
        {
            glBindVertexArray(NativeProbe::PipelineVertexArrayName(Pipeline));
            glBindVertexBuffer(0, NativeProbe::BufferName(Vertices), 0, Pipeline->GetDesc().VertexBindings[0].Stride);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, NativeProbe::BufferName(Indices));
        }
        void Unbind() const { glBindVertexArray(0); }
    };

    class GatedProvider final : public IAssetProvider
    {
    public:
        mutable std::mutex                            Mutex;
        mutable std::condition_variable               Changed;
        std::map<std::string, std::vector<std::byte>> Files;
        bool                                          Closed = false;

        std::variant<std::vector<std::byte>, AssetError> Read(const ProductRecord& record) const override
        {
            std::unique_lock<std::mutex> lock(Mutex);
            Changed.wait(lock, [&] { return !Closed; });
            return Files.at(record.Location.RelativeFile.ToString());
        }
        void SetClosed(bool closed)
        {
            std::lock_guard<std::mutex> lock(Mutex);
            Closed = closed;
            Changed.notify_all();
        }
    };

    class NativeUIFrame
    {
        std::unique_ptr<RenderHardware::ImGuiRenderer> UI;
        bool                                           Rendered = false, Ended = false;

    public:
        NativeUIFrame()
        {
            ImGui::CreateContext();
            auto& io       = ImGui::GetIO();
            io.DisplaySize = {16, 16};
            io.DeltaTime   = 1.0f / 60;
            io.IniFilename = nullptr;
            UI             = RenderHardware::CreateImGuiRenderer(RenderHardware::Backend::OpenGL, nullptr);
            UI->NewFrame();
            ImGui::NewFrame();
            ImGui::GetBackgroundDrawList()->AddRectFilled({0, 0}, {16, 16}, IM_COL32(64, 128, 192, 255));
        }
        void Draw()
        {
            if (!Rendered)
            {
                ImGui::Render();
                Rendered = true;
            }
            UI->Render(ImGui::GetDrawData());
        }
        void End()
        {
            if (!Ended)
            {
                UI->EndFrame();
                Ended = true;
            }
        }
        ~NativeUIFrame()
        {
            End();
            UI.reset();
            ImGui::DestroyContext();
        }
    };

    class RenderAssetCacheGL : public testing::Test
    {
    protected:
        virtual GpuTestBackend TestBackend() const { return GpuTestBackend::OpenGL; }
        void SetUp() override
        {
            if (!Log::GetCoreLogger())
                Log::Init();
            GpuContext = std::make_unique<GpuTestContext>(TestBackend());
            ASSERT_NO_FATAL_FAILURE(GpuContext->SetUp());
            previousBackend = RenderHardware::GetBackend();
            if (TestBackend() == GpuTestBackend::OpenGL)
                ASSERT_EQ(glGetError(), GL_NO_ERROR);
        }

        void TearDown() override
        {
            Provider->SetClosed(false);
            GpuContext.reset();
            RenderHardware::SetBackend(previousBackend);
        }

        std::vector<uint8_t> ReadImage(const RenderHardware::Handle<RenderHardware::Texture>& image)
        {
            auto        device     = RenderHardware::GetSharedDevice();
            const auto& desc       = image->GetDesc();
            size_t      pixelBytes = desc.PixelFormat == RenderHardware::Format::RGBA32Float ? 16 : 4;
            size_t      bytes      = size_t(desc.Width) * desc.Height * pixelBytes;
            auto        buffer     = device->CreateBuffer(
                {bytes, RenderHardware::BufferUsage::CopyDestination, RenderHardware::MemoryUsage::Readback});
            auto commands = device->CreateCommandList();
            commands->CopyTextureToBuffer(image, {}, buffer);
            device->Submit(commands);
            device->WaitIdle();
            auto pixels = buffer->Read(0, bytes);
            if (device->GetCoordinateConventions().Origin == RenderHardware::FramebufferOrigin::UpperLeft)
                for (uint32_t y = 0; y < desc.Height / 2; ++y)
                    std::swap_ranges(pixels.begin() + y * desc.Width * pixelBytes,
                                     pixels.begin() + (y + 1) * desc.Width * pixelBytes,
                                     pixels.begin() + (desc.Height - 1 - y) * desc.Width * pixelBytes);
            return pixels;
        }
        float ReadUniformFirstFloat(const RenderHardware::Handle<RenderHardware::Buffer>& buffer)
        {
            using namespace RenderHardware;
            auto       device = GetSharedDevice();
            ShaderData code;
            code.Code = CompileTestMaterialShader(
                "#version 450\nvoid main(){vec2 "
                "p[3]=vec2[](vec2(-1,-1),vec2(3,-1),vec2(-1,3));gl_Position=vec4(p[gl_VertexIndex],0,1);}",
                "#version 450\nlayout(set=1,binding=0,std140)uniform Value{vec4 First;}p;layout(location=0)out vec4 "
                "c;void main(){c=p.First;}");
            auto         layout = device->CreateBindGroupLayout({1, {{0, BindingType::UniformBuffer}}});
            PipelineDesc pipeline;
            pipeline.Program         = device->CreateShader(code);
            pipeline.GroupLayouts    = {layout};
            pipeline.Rasterizer.Cull = CullMode::None;
            pipeline.ColorFormats    = {Format::RGBA32Float};
            pipeline.Blends.resize(1);
            TextureDesc targetDesc;
            targetDesc.PixelFormat = Format::RGBA32Float;
            targetDesc.Usage = RenderHardware::TextureUsage::RenderTarget | RenderHardware::TextureUsage::CopySource;
            auto          target = device->CreateTexture(targetDesc);
            RenderingDesc scope;
            scope.Colors  = {{target}};
            auto commands = device->CreateCommandList();
            commands->BeginRendering(scope);
            commands->SetViewport({0, 0, 1, 1});
            commands->BindPipeline(device->CreatePipeline(pipeline));
            commands->BindGroup(device->CreateBindGroup({layout, {{0, buffer}}}));
            commands->Draw(3);
            commands->EndRendering();
            device->Submit(commands);
            auto  bytes = ReadImage(target);
            float value;
            std::memcpy(&value, bytes.data(), sizeof(value));
            return value;
        }

        ProductRecord Store(AssetID id, AssetType type, std::vector<std::byte> bytes)
        {
            ProductRecord record;
            record.Metadata.assetID       = id;
            record.Metadata.Type          = type;
            record.Location.RelativeFile  = AssetPath("product-" + std::to_string(++Version));
            record.Location.ByteSize      = bytes.size();
            record.Location.ContentDigest = ComputeContentDigest(bytes);
            std::lock_guard<std::mutex> lock(Provider->Mutex);
            Provider->Files.emplace(record.Location.RelativeFile.ToString(), std::move(bytes));
            return record;
        }
        static MeshData Triangles(size_t count)
        {
            MeshData mesh;
            for (size_t i = 0; i < count * 3; ++i)
            {
                MeshVertexData vertex;
                vertex.Position = {float(i), float(i % 3), 0};
                mesh.Vertices.push_back(vertex);
                mesh.Indices.push_back(uint32_t(i));
            }
            return mesh;
        }
        ProductRecord Mesh(AssetID id, size_t triangles)
        {
            return Store(
                id, AssetType::StaticMesh, std::get<std::vector<std::byte>>(EncodeMeshProduct(Triangles(triangles))));
        }
        ProductRecord Texture(AssetID id, std::byte red)
        {
            TextureData image;
            image.Width = image.Height = 1;
            image.Mips                 = {{{red, std::byte {0}, std::byte {0}, std::byte {255}}}};
            return Store(id, AssetType::Texture, std::get<std::vector<std::byte>>(EncodeTextureProduct(image)));
        }
        static Ref<const AssetRegistry> Registry(const std::vector<ProductRecord>& records)
        {
            return CreateRef<const AssetRegistry>(std::get<AssetRegistry>(AssetRegistry::FromProducts(records)));
        }
        // Simulates render frames until `ready` holds or a timeout expires.
        template<typename Ready>
        static bool Frames(RenderAssetCache& cache, Ready&& ready)
        {
            const auto deadline = std::chrono::steady_clock::now() + 5s;
            while (std::chrono::steady_clock::now() < deadline)
            {
                cache.BeginFrame();
                if (ready())
                    return true;
                std::this_thread::sleep_for(1ms);
            }
            return false;
        }

        Ref<GatedProvider> Provider = CreateRef<GatedProvider>();
        SourceAssetID      Source   = SourceAssetID::Generate();
        unsigned           Version  = 0;
        RenderHardware::Backend previousBackend {};
        std::unique_ptr<GpuTestContext> GpuContext;
    };

    class RenderAssetCacheGPU : public RenderAssetCacheGL, public testing::WithParamInterface<GpuTestBackend>
    {
    protected:
        GpuTestBackend TestBackend() const override { return GetParam(); }
    };

    TEST_F(RenderAssetCacheGL, UploadMeshCreatesIndexedVertexArrayWithEngineLayout)
    {
        auto data     = Triangles(2);
        data.Material = {Source, 9};
        auto uploaded = UploadMesh(data);
        ASSERT_TRUE(std::holds_alternative<GpuMesh>(uploaded)) << std::get<AssetError>(uploaded).Message;
        const auto& mesh = std::get<GpuMesh>(uploaded);
        EXPECT_EQ(mesh.IndexCount, 6u);
        EXPECT_EQ(mesh.Material, data.Material);
        NativeMeshArray array(mesh);
        array.Bind();
        GLint elements = 0;
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &elements);
        EXPECT_NE(elements, 0);
        for (GLuint attribute = 0; attribute < 5; ++attribute)
        {
            GLint enabled = 0, size = 0;
            glGetVertexAttribiv(attribute, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);
            glGetVertexAttribiv(attribute, GL_VERTEX_ATTRIB_ARRAY_SIZE, &size);
            EXPECT_TRUE(enabled) << attribute;
            EXPECT_EQ(size, (std::array<GLint, 5> {3, 3, 2, 4, 4}[attribute])) << attribute;
        }
        array.Unbind();
        EXPECT_EQ(glGetError(), GL_NO_ERROR);

        data.Indices[0] = 100;
        EXPECT_EQ(std::get<AssetError>(UploadMesh(data)).Code, AssetErrorCode::InvalidData);
    }

    TEST_P(RenderAssetCacheGPU, ResourcesAppearWhenLoadedAndInvalidIDsReturnNothing)
    {
        const AssetID    meshID {Source, 1}, textureID {Source, 2};
        AssetManager     assets(Provider, Registry({Mesh(meshID, 1), Texture(textureID, std::byte {200})}));
        RenderAssetCache cache(assets);
        Provider->SetClosed(true);
        cache.BeginFrame();
        EXPECT_EQ(cache.GetMesh(meshID), nullptr);
        EXPECT_EQ(cache.GetTexture(textureID), nullptr);
        Provider->SetClosed(false);
        ASSERT_TRUE(Frames(cache, [&] { return cache.GetMesh(meshID) && cache.GetTexture(textureID); }));
        EXPECT_EQ(cache.GetMesh(meshID)->IndexCount, 3u);
        EXPECT_EQ(cache.GetTexture(textureID)->GetWidth(), 1u);
        EXPECT_EQ(cache.GetMesh(meshID), cache.GetMesh(meshID));

        EXPECT_EQ(cache.GetMesh(AssetID {}), nullptr);
        EXPECT_EQ(cache.GetMaterial(AssetID {}), nullptr);
        // A missing or wrongly typed asset never becomes available.
        cache.BeginFrame();
        EXPECT_EQ(cache.GetMesh({Source, 77}), nullptr);
        for (int frame = 0; frame < 20; ++frame)
        {
            cache.BeginFrame();
            EXPECT_EQ(cache.GetMesh(textureID), nullptr);
            std::this_thread::sleep_for(1ms);
        }
        if (TestBackend() == GpuTestBackend::OpenGL)
            EXPECT_EQ(glGetError(), GL_NO_ERROR);
    }

    TEST_P(RenderAssetCacheGPU, ReimportedAssetKeepsLastUploadUntilReplacementIsReady)
    {
        const AssetID    meshID {Source, 1};
        AssetManager     assets(Provider, Registry({Mesh(meshID, 1)}));
        RenderAssetCache cache(assets);
        ASSERT_TRUE(Frames(cache, [&] { return cache.GetMesh(meshID) != nullptr; }));
        const GpuMesh* original = cache.GetMesh(meshID);
        ASSERT_EQ(original->IndexCount, 3u);

        Provider->SetClosed(true);
        ASSERT_TRUE(std::holds_alternative<ProductChangeSet>(assets.SetRegistry(Registry({Mesh(meshID, 4)}))));
        cache.BeginFrame();
        const GpuMesh* during = cache.GetMesh(meshID);
        ASSERT_NE(during, nullptr);
        EXPECT_EQ(during->IndexCount, 3u);

        Provider->SetClosed(false);
        ASSERT_TRUE(Frames(cache, [&] { return cache.GetMesh(meshID)->IndexCount == 12u; }));
        if (TestBackend() == GpuTestBackend::OpenGL)
            EXPECT_EQ(glGetError(), GL_NO_ERROR);
    }

    TEST_P(RenderAssetCacheGPU, ModelInstancesResolveMeshesAndUnusedEntriesAreEvicted)
    {
        const AssetID meshID {Source, 1}, modelID {Source, 2};
        ModelData     model;
        model.Nodes.push_back({"Root", -1, {1, 0, 0, 2, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, {meshID}});
        auto modelRecord =
            Store(modelID, AssetType::Model, std::get<std::vector<std::byte>>(EncodeModelProduct(model)));
        AssetManager     assets(Provider, Registry({Mesh(meshID, 1), modelRecord}));
        RenderAssetCache cache(assets);
        ASSERT_TRUE(Frames(cache, [&] { return cache.GetModel(modelID) && cache.GetMesh(meshID); }));
        const auto* instances = cache.GetModel(modelID);
        ASSERT_EQ(instances->Instances.size(), 1u);
        EXPECT_EQ(instances->Instances[0].Mesh, meshID);
        EXPECT_EQ(glm::vec3(instances->Instances[0].Transform * glm::vec4(0, 0, 0, 1)), glm::vec3(2, 0, 0));

        // Unused for longer than the limit: the GPU resource and its CPU reference are dropped.
        for (int i = 0; i < 3; ++i)
            cache.BeginFrame(1);
        EXPECT_EQ(assets.CollectUnused(), 2u);
        ASSERT_TRUE(Frames(cache, [&] { return cache.GetMesh(meshID) != nullptr; }));
        if (TestBackend() == GpuTestBackend::OpenGL)
            EXPECT_EQ(glGetError(), GL_NO_ERROR);
    }

    class StandardMaterialGL : public RenderAssetCacheGL, public testing::WithParamInterface<GpuTestBackend>
    {
    protected:
        GpuTestBackend TestBackend() const override { return GetParam(); }
        void SetUp() override
        {
            ASSERT_NO_FATAL_FAILURE(RenderAssetCacheGL::SetUp());
            Assets = CreateScope<AssetManager>(Provider, Registry({}));
            Cache       = CreateScope<RenderAssetCache>(*Assets);
            auto device = Cache->GetDevice();
            using namespace RenderHardware;
            TextureDesc color;
            color.Width = color.Height = 16;
            color.PixelFormat          = Format::RGBA32Float;
            color.Usage = RenderHardware::TextureUsage::RenderTarget | RenderHardware::TextureUsage::CopySource;
            Color       = device->CreateTexture(color);
            TextureDesc environment;
            environment.Dimension   = TextureDimension::Cube;
            environment.PixelFormat = Format::RGBA32Float;
            environment.Usage = RenderHardware::TextureUsage::Sampled | RenderHardware::TextureUsage::CopyDestination;
            Environment[0]    = device->CreateTexture(environment);
            Environment[1]    = device->CreateTexture(environment);
            SetIrradiance({0, 0, 0, 1});
            UploadEnvironment(Environment[1], {0, 0, 0, 1});
            environment.Dimension = TextureDimension::Texture2D;
            Brdf                  = device->CreateTexture(environment);
            UploadEnvironment(Brdf, {0, 0, 0, 1});
            SamplerDesc sampler;
            sampler.Min = sampler.Mag = sampler.Mip = Filter::Nearest;
            Sampling                                = device->CreateSampler(sampler);
            Frame.CameraPosition = {0, 0, 2, 1};
            for (auto& light : Frame.LightPositions)
                light = {0, 0, 1, 1};
            FrameBuffer  = CreateScope<GpuUniformBuffer>(sizeof(Frame));
            ObjectBuffer = CreateScope<GpuUniformBuffer>(sizeof(Object));
        }
        void UploadEnvironment(const RenderHardware::Handle<RenderHardware::Texture>& image,
                               const std::array<float, 4>&                            value)
        {
            auto device = Cache->GetDevice();
            auto buffer = device->CreateBuffer(
                {sizeof(value), RenderHardware::BufferUsage::CopySource, RenderHardware::MemoryUsage::Upload});
            buffer->Write(0, value.data(), sizeof(value));
            auto commands = device->CreateCommandList();
            for (uint32_t face = 0;
                 face < (image->GetDesc().Dimension == RenderHardware::TextureDimension::Cube ? 6u : 1u);
                 ++face)
                commands->CopyBufferToTexture(buffer, 0, image, {0, face});
            device->Submit(commands);
            device->WaitIdle();
        }
        void SetIrradiance(const std::array<float, 4>& value) { UploadEnvironment(Environment[0], value); }
        void TearDown() override
        {
            Pipelines.clear();
            Cache.reset();
            Assets.reset();
            FrameBuffer.reset();
            ObjectBuffer.reset();
            Color.reset();
            Environment = {};
            Brdf.reset();
            Sampling.reset();
            RenderAssetCacheGL::TearDown();
        }
        AssetID Image(const std::vector<std::array<float, 4>>& pixels)
        {
            TextureData image;
            image.Width  = uint32_t(pixels.size());
            image.Height = 1;
            image.Format = TexturePixelFormat::RGBA32Float;
            image.Usage  = TextureUsage::Data;
            image.Mips.push_back({std::vector<std::byte>(pixels.size() * sizeof(pixels[0]))});
            std::memcpy(image.Mips[0].Pixels.data(), pixels.data(), image.Mips[0].Pixels.size());
            const AssetID id {Source, ++NextID};
            Records.push_back(
                Store(id, AssetType::Texture, std::get<std::vector<std::byte>>(EncodeTextureProduct(image))));
            const auto registry = Assets->SetRegistry(Registry(Records));
            EXPECT_TRUE(std::holds_alternative<ProductChangeSet>(registry));
            EXPECT_TRUE(Frames(*Cache, [&] { return Cache->GetTexture(id) != nullptr; }));
            RenderHardware::SamplerDesc sampling;
            sampling.Min = sampling.Mag = sampling.Mip = RenderHardware::Filter::Nearest;
            Cache->SetTextureSampler(id, sampling);
            return id;
        }
        std::array<float, 4> Draw(const MaterialSource& source,
                                  std::array<float, 4>  tangent     = {1, 0, 0, 1},
                                  int                   x           = 8,
                                  std::array<float, 4>  vertexColor = {1, 1, 1, 1})
        {
            auto resolved = ResolveMaterialSource(source, nullptr);
            if (const auto* error = std::get_if<AssetError>(&resolved))
                throw std::runtime_error(error->Message);
            auto uploaded = MakeGpuMaterialBindings(std::get<MaterialData>(resolved), GetStandardShader());
            if (const auto* error = std::get_if<AssetError>(&uploaded))
                throw std::runtime_error(error->Message);
            const auto& bindings = std::get<GpuMaterialBindings>(uploaded);
            return DrawBindings(bindings, tangent, x, vertexColor);
        }
        std::array<float, 4> DrawBindings(const GpuMaterialBindings& bindings,
                                          std::array<float, 4>       tangent     = {1, 0, 0, 1},
                                          int                        x           = 8,
                                          std::array<float, 4>       vertexColor = {1, 1, 1, 1})
        {
            const auto* shader = Cache->GetShader(bindings.Shader);
            if (!shader)
                throw std::runtime_error("GPU shader failed");
            auto frame = Frame;
            frame.ViewProjection =
                DeviceProjection(frame.ViewProjection, Cache->GetDevice()->GetCoordinateConventions());
            FrameBuffer->Update(&frame, sizeof(frame));
            ObjectBuffer->Update(&Object, sizeof(Object));
            auto     material = Cache->GetMaterialGroup(*shader, bindings);
            MeshData quad;
            for (auto [px, py] : {std::pair {-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}})
            {
                MeshVertexData vertex;
                vertex.Position = {px, py, 0};
                vertex.Normal   = {0, 0, 1};
                vertex.UV       = {(px + 1) / 2, (py + 1) / 2};
                vertex.Tangent  = tangent;
                vertex.Color    = vertexColor;
                quad.Vertices.push_back(vertex);
            }
            quad.Indices = {0, 1, 2, 2, 3, 0};
            auto mesh    = UploadMesh(quad);
            if (const auto* error = std::get_if<AssetError>(&mesh))
                throw std::runtime_error(error->Message);
            auto        device   = Cache->GetDevice();
            const auto& geometry = std::get<GpuMesh>(mesh);
            using namespace RenderHardware;
            auto& pipeline = Pipelines[shader->Program];
            if (!pipeline)
            {
                PipelineDesc desc;
                desc.Program          = shader->Program;
                desc.GroupLayouts     = {shader->EngineLayout, shader->MaterialLayout};
                desc.VertexBindings   = geometry.VertexBindings;
                desc.Attributes       = geometry.Attributes;
                desc.Rasterizer.Cull  = CullMode::None;
                desc.Rasterizer.Front = FrontFace::CounterClockwise;
                desc.ColorFormats     = {Format::RGBA32Float};
                desc.Blends.resize(1);
                pipeline = device->CreatePipeline(desc);
            }
            BindGroupDesc engine;
            engine.Layout = shader->EngineLayout;
            for (const auto& binding : shader->EngineResources)
            {
                BindGroupEntry entry;
                entry.Slot = binding.Binding;
                if (binding.Binding < 2)
                    entry.BufferResource = binding.Binding == 0 ? FrameBuffer->Resource() : ObjectBuffer->Resource();
                else
                {
                    entry.TextureResource = binding.Binding == 4 ? Brdf : Environment[binding.Binding - 2];
                    entry.SamplerResource = Sampling;
                }
                engine.Entries.push_back(entry);
            }
            auto          commands = device->CreateCommandList();
            RenderingDesc target;
            target.Colors = {{Color, {}, LoadOp::Clear, StoreOp::Store, {0, 0, 0, 1}}};
            commands->BeginRendering(target);
            commands->SetViewport({0, 0, 16, 16, 0, 1});
            commands->BindPipeline(pipeline);
            commands->BindGroup(device->CreateBindGroup(engine));
            commands->BindGroup(material);
            commands->BindVertexBuffers({{0, geometry.Vertices, 0}});
            commands->BindIndexBuffer(geometry.Indices, IndexType::Uint32);
            commands->DrawIndexed(6);
            commands->EndRendering();
            auto buffer = device->CreateBuffer({16 * 16 * 16, BufferUsage::CopyDestination, MemoryUsage::Readback});
            commands->CopyTextureToBuffer(Color, {}, buffer);
            device->Submit(commands);
            device->WaitIdle();
            auto                 bytes = buffer->Read(0, 16 * 16 * 16);
            std::array<float, 4> pixel {};
            auto                 row = DeviceRow(8, 16, device->GetCoordinateConventions());
            std::memcpy(pixel.data(), bytes.data() + (row * 16 + x) * 16, 16);
            return pixel;
        }
        static float               Encode(float linear) { return std::pow(linear / (linear + 1), 1.0f / 2.2f); }
        Scope<AssetManager>        Assets;
        Scope<RenderAssetCache>    Cache;
        std::vector<ProductRecord> Records;
        uint64_t                   NextID = 0;
        EngineFrameUniforms        Frame;
        EngineObjectUniforms       Object;
        Scope<GpuUniformBuffer>    FrameBuffer, ObjectBuffer;
        RenderHardware::Handle<RenderHardware::Texture>                Color, Brdf;
        std::array<RenderHardware::Handle<RenderHardware::Texture>, 2> Environment;
        RenderHardware::Handle<RenderHardware::Sampler>                Sampling;
        std::map<RenderHardware::Handle<RenderHardware::Shader>, RenderHardware::Handle<RenderHardware::Pipeline>>
            Pipelines;
    };

    TEST_P(StandardMaterialGL, PackingFailureLogsOnceDrawsStandardAndRetriesEitherChangedSource)
    {
        // A stale recipe can load but fail packing; retrying a null cache value floods logs every frame.
        const AssetID shaderID {Source, 100}, materialID {Source, 101};
        ShaderData    shader;
        shader.Code = CompileTestMaterialShader();
        MaterialSource source;
        source.Shader     = shaderID;
        source.Parameters = {{"Tint", {0, 1, 0, 1}}, {"Strength", {1}}};
        const auto good   = std::get<MaterialData>(ResolveMaterialSource(source, &shader));
        auto       bad    = good;
        // Three floats fit a later vec3 layout, but cannot pack the current vec4 Tint.
        bad.Bindings->Uniforms.at(0).Values.at(0).Values = std::vector<float> {0, 1, 0};
        auto materialRecord =
            Store(materialID, AssetType::Material, std::get<std::vector<std::byte>>(EncodeMaterialProduct(bad)));
        auto shaderRecord =
            Store(shaderID, AssetType::Shader, std::get<std::vector<std::byte>>(EncodeShaderProduct(shader.Code)));
        ASSERT_TRUE(
            std::holds_alternative<ProductChangeSet>(Assets->SetRegistry(Registry({materialRecord, shaderRecord}))));
        ASSERT_TRUE(Frames(*Cache, [&] { return Cache->GetMaterial(materialID) && Cache->GetShader(shaderID); }));
        std::ostringstream errors;
        auto               sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(errors);
        sink->set_level(spdlog::level::err);
        struct Capture
        {
            spdlog::sink_ptr Sink;
            explicit Capture(spdlog::sink_ptr sink) : Sink(std::move(sink))
            {
                Log::GetCoreLogger()->sinks().push_back(Sink);
            }
            ~Capture()
            {
                auto& sinks = Log::GetCoreLogger()->sinks();
                sinks.erase(std::remove(sinks.begin(), sinks.end(), Sink), sinks.end());
            }
        } capture(sink);
        const auto errorCount = [&] {
            const auto log = errors.str();
            return std::count(log.begin(), log.end(), '\n');
        };
        Frame.DebugView = 1;
        for (int frame = 0; frame < 4; ++frame)
        {
            Cache->BeginFrame();
            const auto* bindings = Cache->GetMaterialBindings(materialID);
            ASSERT_NE(bindings, nullptr);
            EXPECT_EQ(bindings->Shader, StandardShaderID());
            const auto pixel = DrawBindings(*bindings);
            for (int channel = 0; channel < 3; ++channel)
                EXPECT_EQ(pixel[channel], 1);
            // Debug fallback must not overwrite the remembered custom failure either.
            ASSERT_NE(Cache->GetMaterialBindings(materialID, true), nullptr);
        }
        EXPECT_EQ(errorCount(), 1);
        EXPECT_NE(errors.str().find("Material " + materialID.ToString()), std::string::npos);
        // Changing only the material must clear its cached failure and restore custom drawing.
        materialRecord =
            Store(materialID, AssetType::Material, std::get<std::vector<std::byte>>(EncodeMaterialProduct(good)));
        ASSERT_TRUE(
            std::holds_alternative<ProductChangeSet>(Assets->SetRegistry(Registry({materialRecord, shaderRecord}))));
        ASSERT_TRUE(Frames(*Cache, [&] {
            const auto* bindings = Cache->GetMaterialBindings(materialID);
            return bindings && bindings->Shader == shaderID;
        }));
        const auto recoveredMaterial = DrawBindings(*Cache->GetMaterialBindings(materialID));
        EXPECT_EQ(recoveredMaterial[0], 0);
        EXPECT_EQ(recoveredMaterial[1], 1);
        EXPECT_EQ(recoveredMaterial[2], 0);
        EXPECT_EQ(errorCount(), 1);
        materialRecord =
            Store(materialID, AssetType::Material, std::get<std::vector<std::byte>>(EncodeMaterialProduct(bad)));
        ASSERT_TRUE(
            std::holds_alternative<ProductChangeSet>(Assets->SetRegistry(Registry({materialRecord, shaderRecord}))));
        ASSERT_TRUE(Frames(*Cache, [&] {
            const auto* bindings = Cache->GetMaterialBindings(materialID);
            return bindings && bindings->Shader == StandardShaderID();
        }));
        EXPECT_EQ(errorCount(), 2);
        // Changing only the shader to match that recipe must also clear its cached failure.
        std::string fragment = TestMaterialFragment;
        fragment.replace(fragment.find("vec4  Tint"), std::string("vec4  Tint").size(), "vec3  Tint");
        fragment.replace(fragment.find("p.Tint *"), std::string("p.Tint *").size(), "vec4(p.Tint, 1) *");
        shader.Code = CompileTestMaterialShader(TestMaterialVertex, fragment);
        shaderRecord =
            Store(shaderID, AssetType::Shader, std::get<std::vector<std::byte>>(EncodeShaderProduct(shader.Code)));
        ASSERT_TRUE(
            std::holds_alternative<ProductChangeSet>(Assets->SetRegistry(Registry({materialRecord, shaderRecord}))));
        ASSERT_TRUE(Frames(*Cache, [&] {
            const auto* bindings = Cache->GetMaterialBindings(materialID);
            return bindings && bindings->Shader == shaderID;
        }));
        const auto recoveredShader = DrawBindings(*Cache->GetMaterialBindings(materialID));
        EXPECT_EQ(recoveredShader[0], 0);
        EXPECT_EQ(recoveredShader[1], 1);
        EXPECT_EQ(recoveredShader[2], 0);
        EXPECT_EQ(errorCount(), 2);
    }

    TEST_P(StandardMaterialGL, UnassignedNormalIsGeometricAndUnassignedEmissionIsDark)
    {
        // White in the normal slot rotates +Z toward (1,1,1); flat must stay +Z.
        Frame.DebugView = 2;
        for (const auto tangent : {std::array<float, 4> {1, 0, 0, 1}, std::array<float, 4> {0, 0, 0, 0}})
        {
            const auto normal = Draw({}, tangent);
            EXPECT_NEAR(normal[0], 0.5f, 1e-6f);
            EXPECT_NEAR(normal[1], 0.5f, 1e-6f);
            EXPECT_NEAR(normal[2], 1.0f, 1e-6f);
        }
        Frame.DebugView = 0;
        const auto dark = Draw({});
        EXPECT_EQ(dark[0], 0);
        EXPECT_EQ(dark[1], 0);
        EXPECT_EQ(dark[2], 0);
    }

    TEST_P(StandardMaterialGL, VertexColorIsOptInAndStrengthOneAppliesIt)
    {
        // Character vertex colors may contain auxiliary data; enabling tint by default changes existing looks.
        Frame.DebugView = 1;
        MaterialSource source;
        source.Parameters["BaseColor"] = {0.5, 0.25, 1, 1};
        const auto ignored             = Draw(source, {1, 0, 0, 1}, 8, {0.25, 0.5, 0, 1});
        EXPECT_NEAR(ignored[0], std::pow(0.5f, 1.0f / 2.2f), 2e-6f);
        EXPECT_NEAR(ignored[1], std::pow(0.25f, 1.0f / 2.2f), 2e-6f);
        EXPECT_EQ(ignored[2], 1);
        source.Parameters["VertexColorStrength"] = {1};
        const auto tinted                        = Draw(source, {1, 0, 0, 1}, 8, {0.25, 0.5, 0, 1});
        EXPECT_NEAR(tinted[0], std::pow(0.125f, 1.0f / 2.2f), 2e-6f);
        EXPECT_NEAR(tinted[1], std::pow(0.125f, 1.0f / 2.2f), 2e-6f);
        EXPECT_EQ(tinted[2], 0);
    }

    TEST_P(StandardMaterialGL, TangentDirectionHandednessAndDerivativeFallbackOrientNormalMap)
    {
        Frame.DebugView = 2;
        MaterialSource source;
        source.Textures["NormalMap"] = Image({{0.5f, 1, 0.5f, 1}}); // Tangent-space +Y.
        const auto positive          = Draw(source);
        const auto negative          = Draw(source, {1, 0, 0, -1});
        EXPECT_NEAR(positive[1], 1.0f, 1e-6f);
        EXPECT_NEAR(negative[1], 0.0f, 1e-6f);
        // A real tangent takes precedence over UV derivatives: +Y tangent gives -X bitangent.
        const auto rotated = Draw(source, {0, 1, 0, 1});
        EXPECT_NEAR(rotated[0], 0.0f, 1e-6f);
        const auto derivative = Draw(source, {0, 0, 0, 0});
        EXPECT_NEAR(derivative[1], 1.0f, 1e-6f);
        Object.Transform[0][0] = -1;
        const auto mirrored    = Draw(source);
        EXPECT_NEAR(mirrored[1], 1.0f, 1e-6f); // Mirroring X must preserve the world +Y bitangent.
        source.Textures["NormalMap"]        = Image({{0.8f, 0.5f, 0.9f, 1}});
        source.Parameters["NormalStrength"] = {0};
        const auto disabled                 = Draw(source);
        EXPECT_NEAR(disabled[0], 0.5f, 1e-6f);
        EXPECT_NEAR(disabled[2], 1.0f, 1e-6f);
    }

    TEST_P(StandardMaterialGL, UvTilingRepeatsAndOffsetIsAppliedAfterScaling)
    {
        Frame.DebugView = 1;
        MaterialSource source;
        source.Textures["BaseColorMap"] = Image({{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}, {1, 1, 1, 1}});
        source.Parameters["UVTiling"]   = {2, 1};
        source.Parameters["UVOffset"]   = {0.25, 0};
        // Pixel x=0: u=1/32; 2*u+1/4=5/16 selects green. (u+offset)*2 selects blue.
        for (int x : {0, 8})
        {
            const auto pixel = Draw(source, {1, 0, 0, 1}, x);
            EXPECT_EQ(pixel[0], 0);
            EXPECT_EQ(pixel[1], 1);
            EXPECT_EQ(pixel[2], 0);
        }
    }

    TEST_P(StandardMaterialGL, EmissiveFactorAndMapMultiplyBeforeToneMapping)
    {
        // Zero lighting isolates emission and catches missing factor, intensity, map or gamma.
        MaterialSource source;
        source.Parameters["EmissiveColor"]     = {0.25, 0.5, 0.75};
        source.Parameters["EmissiveIntensity"] = {2};
        const auto factor                      = Draw(source);
        for (int channel = 0; channel < 3; ++channel)
            EXPECT_NEAR(factor[channel], Encode((channel + 1) * 0.5f), 2e-6f);
        source.Textures["EmissiveMap"] = Image({{0.5f, 0.25f, 0, 1}});
        const auto mapped              = Draw(source);
        EXPECT_NEAR(mapped[0], Encode(0.25f), 2e-6f);
        EXPECT_NEAR(mapped[1], Encode(0.25f), 2e-6f);
        EXPECT_EQ(mapped[2], 0);
        // A pending map must use the same white fallback as an unassigned map, preserving the factor.
        source.Textures["EmissiveMap"] = {Source, 999};
        const auto pending             = Draw(source);
        for (int channel = 0; channel < 3; ++channel)
            EXPECT_NEAR(pending[channel], Encode((channel + 1) * 0.5f), 2e-6f);
        source.Parameters.erase("EmissiveColor");
        source.Textures["EmissiveMap"] = Image({{1, 1, 1, 1}});
        const auto zeroFactor          = Draw(source);
        EXPECT_EQ(zeroFactor[0] + zeroFactor[1] + zeroFactor[2], 0);
    }

    TEST_P(StandardMaterialGL, StandardFactorsShadeWithSceneLightsAndGltfPackedChannels)
    {
        for (auto& light : Frame.LightColors)
            light = {2, 2, 2, 1};
        MaterialSource source;
        source.Parameters["BaseColor"] = {1, 0, 0, 1};
        const auto lit                 = Draw(source);
        EXPECT_GT(lit[0], 0.6f);
        EXPECT_LT(lit[1], lit[0] / 4);
        for (auto& light : Frame.LightPositions)
            light = {0, 0, -1, 1};
        const auto unlit = Draw(source);
        EXPECT_EQ(unlit[0] + unlit[1] + unlit[2], 0);
        for (auto& light : Frame.LightPositions)
            light = {0, 0, 1, 1};
        source.Parameters["BaseColor"]          = {0.4, 0.2, 0.1, 1};
        source.Parameters["Metallic"]           = {1};
        source.Parameters["Roughness"]          = {1};
        source.Textures["MetallicRoughnessMap"] = Image({{0.1f, 0.6f, 0, 1}});
        const auto packed                       = Draw(source);
        source.Textures.erase("MetallicRoughnessMap");
        source.Parameters["Metallic"]  = {0};
        source.Parameters["Roughness"] = {0.6};
        const auto equivalent          = Draw(source);
        // G roughness/B metallic must equal those explicit factors with a white map.
        for (int channel = 0; channel < 3; ++channel)
            EXPECT_NEAR(packed[channel], equivalent[channel], 2e-6f);
    }

    TEST_P(StandardMaterialGL, OcclusionRedChannelAndStrengthOnlyAttenuateIbl)
    {
        // Known diffuse IBL: dielectric kD=0.96, irradiance=0.5, albedo=0.25.
        SetIrradiance({0.5f, 0.5f, 0.5f, 1});
        MaterialSource source;
        source.Parameters["BaseColor"]         = {0.25, 0.25, 0.25, 1};
        source.Textures["OcclusionMap"]        = Image({{0.2f, 0.7f, 0.9f, 1}});
        source.Parameters["OcclusionStrength"] = {0.5};
        const auto occluded                    = Draw(source);
        EXPECT_NEAR(occluded[0], Encode(0.96f * 0.5f * 0.25f * 0.6f), 2e-6f);
        source.Parameters["OcclusionStrength"] = {0};
        const auto full                        = Draw(source);
        EXPECT_NEAR(full[0], Encode(0.96f * 0.5f * 0.25f), 2e-6f);
        SetIrradiance({0, 0, 0, 1});
        for (auto& light : Frame.LightColors)
            light = {2, 2, 2, 1};
        const auto direct                      = Draw(source);
        source.Parameters["OcclusionStrength"] = {1};
        const auto directOccluded              = Draw(source);
        EXPECT_NEAR(direct[0], directOccluded[0], 2e-6f);
    }

    TEST_P(RenderAssetCacheGPU, CustomSamplerFallbackBindsBlackAndNormalThroughPublishedProduct)
    {
        // Project shaders must get their declared role too, even if no texture assignments exist.
        ShaderData shader;
        shader.Code             = CompileTestMaterialShader();
        shader.TextureFallbacks = {{"u_Albedo", ShaderTextureFallback::Black}};
        for (const auto role : {ShaderTextureFallback::Black, ShaderTextureFallback::Normal})
        {
            shader.TextureFallbacks["u_Albedo"] = role;
            auto product                        = EncodeShaderProduct(shader.Code, {}, {}, shader.TextureFallbacks);
            ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(product));
            const auto& bytes   = std::get<std::vector<std::byte>>(product);
            auto        decoded = DecodeShaderProduct(bytes.data(), bytes.size());
            ASSERT_TRUE(std::holds_alternative<ShaderData>(decoded));
            auto program = GpuShader::Create(std::get<ShaderData>(decoded));
            ASSERT_TRUE(std::holds_alternative<GpuShader>(program));
            AssetManager     assets(Provider, Registry({}));
            RenderAssetCache cache(assets);
            GpuMaterialBindings bindings;
            cache.GetMaterialGroup(std::get<GpuShader>(program), bindings);
            ASSERT_EQ(bindings.Images.size(), 1u);
            const auto           pixels = ReadImage(bindings.Images[0]);
            std::array<float, 4> pixel {};
            std::memcpy(pixel.data(), pixels.data(), sizeof(pixel));
            const auto expected = role == ShaderTextureFallback::Black ? std::array<float, 4> {0, 0, 0, 1} :
                                                                         std::array<float, 4> {0.5f, 0.5f, 1, 1};
            EXPECT_EQ(pixel, expected);
        }
    }

    TEST_P(RenderAssetCacheGPU, MissingFailedAndDebugCustomShadersUseStandardBindings)
    {
        // A missing registry entry or invalid custom contract must still produce drawable Standard UBOs.
        const AssetID shaderID {Source, 1}, materialID {Source, 2};
        MaterialData  material;
        material.Shader   = shaderID;
        material.Bindings = MaterialBindingRecipe {};
        auto materialRecord =
            Store(materialID, AssetType::Material, std::get<std::vector<std::byte>>(EncodeMaterialProduct(material)));
        AssetManager     assets(Provider, Registry({materialRecord}));
        RenderAssetCache cache(assets);
        ASSERT_TRUE(Frames(cache, [&] { return cache.GetMaterial(materialID) != nullptr; }));
        const auto* pending = cache.GetMaterialBindings(materialID);
        ASSERT_NE(pending, nullptr);
        EXPECT_EQ(pending->Shader, StandardShaderID());
        ASSERT_EQ(pending->Blocks.size(), 1u);
        // A shader that loaded successfully but violates the material contract also falls back.
        ShaderData  failed;
        std::string invalidFragment = TestMaterialFragment;
        invalidFragment.insert(invalidFragment.find("void main()"), "layout(set=3,binding=0) uniform sampler2D Bad;\n");
        invalidFragment.insert(invalidFragment.find("p.Tint *"), "texture(Bad,v_UV) + ");
        failed.Code = CompileTestMaterialShader(TestMaterialVertex, invalidFragment);
        auto badRecord =
            Store(shaderID, AssetType::Shader, std::get<std::vector<std::byte>>(EncodeShaderProduct(failed.Code)));
        ASSERT_TRUE(
            std::holds_alternative<ProductChangeSet>(assets.SetRegistry(Registry({materialRecord, badRecord}))));
        ASSERT_TRUE(Frames(cache, [&] { return assets.Request<ShaderDataAsset>(shaderID).IsReady(); }));
        EXPECT_EQ(cache.GetMaterialBindings(materialID)->Shader, StandardShaderID());
        ShaderData valid;
        valid.Code = CompileTestMaterialShader();
        MaterialSource custom;
        custom.Shader             = shaderID;
        custom.Parameters["Tint"] = {0, 1, 0, 1};
        material                  = std::get<MaterialData>(ResolveMaterialSource(custom, &valid));
        materialRecord =
            Store(materialID, AssetType::Material, std::get<std::vector<std::byte>>(EncodeMaterialProduct(material)));
        auto goodRecord =
            Store(shaderID, AssetType::Shader, std::get<std::vector<std::byte>>(EncodeShaderProduct(valid.Code)));
        ASSERT_TRUE(
            std::holds_alternative<ProductChangeSet>(assets.SetRegistry(Registry({materialRecord, goodRecord}))));
        ASSERT_TRUE(Frames(cache, [&] {
            const auto* bindings = cache.GetMaterialBindings(materialID);
            return bindings && bindings->Shader == shaderID;
        }));
        EXPECT_EQ(cache.GetMaterialBindings(materialID, true)->Shader, StandardShaderID());
        EXPECT_EQ(cache.GetMaterialBindings(materialID)->Shader, shaderID);
        // A failed hot reload cannot use the old GL program with the newly loaded CPU layout.
        badRecord =
            Store(shaderID, AssetType::Shader, std::get<std::vector<std::byte>>(EncodeShaderProduct(failed.Code)));
        ASSERT_TRUE(
            std::holds_alternative<ProductChangeSet>(assets.SetRegistry(Registry({materialRecord, badRecord}))));
        ASSERT_TRUE(Frames(cache, [&] { return assets.Request<ShaderDataAsset>(shaderID).IsReady(); }));
        EXPECT_EQ(cache.GetMaterialBindings(materialID)->Shader, StandardShaderID());
        // KMAT v2's Shader identity came before recipes; its PBR factors must survive the adapter.
        material.Bindings.reset();
        material.BaseColor = {0.125f, 0.25f, 0.5f, 1};
        materialRecord =
            Store(materialID, AssetType::Material, std::get<std::vector<std::byte>>(EncodeMaterialProduct(material)));
        ASSERT_TRUE(
            std::holds_alternative<ProductChangeSet>(assets.SetRegistry(Registry({materialRecord, goodRecord}))));
        ASSERT_TRUE(Frames(cache, [&] {
            const auto* bindings = cache.GetMaterialBindings(materialID);
            return bindings && bindings->Shader == StandardShaderID() &&
                   cache.GetMaterial(materialID)->Shader == StandardShaderID();
        }));
        const auto* legacy = cache.GetMaterialBindings(materialID);
        const float red    = ReadUniformFirstFloat(legacy->Blocks.at(0).second.Resource());
        EXPECT_FLOAT_EQ(red, 0.125f);
    }

    // Draws a quad with a Shader material the way SceneRenderer does: engine data in
    // set 0, the material's packed blocks and textures in set 1.
    TEST_P(RenderAssetCacheGPU, ShaderMaterialDrawsWithItsPackedParametersAndTextures)
    {
        const AssetID shaderID {Source, 1}, tintedID {Source, 2}, texturedID {Source, 3}, textureID {Source, 4};
        ShaderData    shader;
        shader.Code = CompileTestMaterialShader();
        MaterialSource tinted;
        tinted.Shader                 = shaderID;
        tinted.Parameters["Tint"]     = {0, 1, 0, 1};
        tinted.Parameters["Strength"] = {1};
        auto textured                 = tinted;
        textured.Parameters["Tint"]   = {1, 1, 1, 1};
        textured.Textures["u_Albedo"] = textureID;
        const auto material           = [&](AssetID id, const MaterialSource& source) {
            auto resolved = ResolveMaterialSource(source, &shader);
            EXPECT_TRUE(std::holds_alternative<MaterialData>(resolved)) << std::get<AssetError>(resolved).Message;
            return Store(id,
                         AssetType::Material,
                         std::get<std::vector<std::byte>>(EncodeMaterialProduct(std::get<MaterialData>(resolved))));
        };
        AssetManager assets(
            Provider,
            Registry({Store(shaderID,
                            AssetType::Shader,
                            std::get<std::vector<std::byte>>(EncodeShaderProduct(shader.Code))),
                      material(tintedID, tinted),
                      material(texturedID, textured),
                      Texture(textureID, std::byte {200})}));
        RenderAssetCache cache(assets);
        ASSERT_TRUE(Frames(cache, [&] {
            return cache.GetMaterialBindings(tintedID) && cache.GetMaterialBindings(texturedID) &&
                   cache.GetTexture(textureID);
        }));
        EXPECT_EQ(cache.GetMaterial(tintedID)->Shader, shaderID);
        const GpuShader* program = cache.GetShader(shaderID);
        ASSERT_NE(program, nullptr);

        constexpr int               Size   = 16;
        auto                        device = cache.GetDevice();
        RenderHardware::TextureDesc colorDesc;
        colorDesc.Width = colorDesc.Height = Size;
        colorDesc.Usage = RenderHardware::TextureUsage::RenderTarget | RenderHardware::TextureUsage::CopySource;
        auto     color  = device->CreateTexture(colorDesc);
        MeshData quad;
        for (auto [x, y] : {std::pair {-1.0f, -1.0f}, {0.0f, -1.0f}, {0.0f, 1.0f}, {-1.0f, 1.0f}})
        {
            MeshVertexData vertex;
            vertex.Position = {x, y, 0};
            quad.Vertices.push_back(vertex);
        }
        quad.Indices  = {0, 1, 2, 2, 3, 0};
        auto uploaded = UploadMesh(quad);
        ASSERT_TRUE(std::holds_alternative<GpuMesh>(uploaded));
        const auto& mesh = std::get<GpuMesh>(uploaded);

        EngineFrameUniforms  frame;
        EngineObjectUniforms object;
        // Moves the quad to the right half of the target.
        object.Transform[3] = glm::vec4(1, 0, 0, 1);
        frame.ViewProjection = DeviceProjection(frame.ViewProjection, device->GetCoordinateConventions());
        GpuUniformBuffer frameBuffer(sizeof(frame), &frame), objectBuffer(sizeof(object), &object);
        RenderHardware::PipelineDesc desc;
        desc.Program         = program->Program;
        desc.GroupLayouts    = {program->EngineLayout, program->MaterialLayout};
        desc.VertexBindings  = mesh.VertexBindings;
        desc.Attributes      = mesh.Attributes;
        desc.Rasterizer.Cull = RenderHardware::CullMode::None;
        desc.ColorFormats    = {RenderHardware::Format::RGBA8Unorm};
        desc.Blends.resize(1);
        auto                          pipeline = device->CreatePipeline(desc);
        RenderHardware::BindGroupDesc engineDesc;
        engineDesc.Layout = program->EngineLayout;
        auto uploads      = device->CreateCommandList();
        for (const auto& binding : program->EngineResources)
        {
            RenderHardware::BindGroupEntry entry;
            entry.Slot = binding.Binding;
            if (binding.Binding < 2)
                entry.BufferResource = binding.Binding == 0 ? frameBuffer.Resource() : objectBuffer.Resource();
            else
            {
                RenderHardware::TextureDesc imageDesc;
                imageDesc.Dimension = binding.Binding == 4 ? RenderHardware::TextureDimension::Texture2D :
                                                             RenderHardware::TextureDimension::Cube;
                imageDesc.Usage = RenderHardware::TextureUsage::Sampled | RenderHardware::TextureUsage::CopyDestination;
                entry.TextureResource = device->CreateTexture(imageDesc);
                entry.SamplerResource = device->CreateSampler();
                uint32_t black        = 0xff000000u;
                auto     buffer       = device->CreateBuffer(
                    {4, RenderHardware::BufferUsage::CopySource, RenderHardware::MemoryUsage::Upload});
                buffer->Write(0, &black, 4);
                for (uint32_t face = 0; face < (binding.Binding == 4 ? 1u : 6u); ++face)
                    uploads->CopyBufferToTexture(buffer, 0, entry.TextureResource, {0, face});
            }
            engineDesc.Entries.push_back(entry);
        }
        device->Submit(uploads);
        auto       engine = device->CreateBindGroup(engineDesc);
        const auto draw = [&](AssetID materialID) -> std::array<uint8_t, 4> {
            const auto* bindings = cache.GetMaterialBindings(materialID);
            RenderHardware::RenderingDesc target;
            target.Colors = {{color, {}, RenderHardware::LoadOp::Clear, RenderHardware::StoreOp::Store, {0, 0, 0, 1}}};
            auto commands = device->CreateCommandList();
            commands->BeginRendering(target);
            commands->SetViewport({0, 0, Size, Size});
            commands->BindPipeline(pipeline);
            commands->BindGroup(engine);
            commands->BindGroup(cache.GetMaterialGroup(*program, *bindings));
            commands->BindVertexBuffers({{0, mesh.Vertices}});
            commands->BindIndexBuffer(mesh.Indices, RenderHardware::IndexType::Uint32);
            commands->DrawIndexed(mesh.IndexCount);
            commands->EndRendering();
            device->Submit(commands);
            auto pixels = ReadImage(color);
            if (TestBackend() == GpuTestBackend::OpenGL)
                EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
            // The left half stays clear: the object transform was applied.
            EXPECT_EQ(pixels[(Size / 2 * Size + Size / 4) * 4 + 1], 0);
            const uint8_t* p = &pixels[(Size / 2 * Size + 3 * Size / 4) * 4];
            return {p[0], p[1], p[2], p[3]};
        };
        // A sampler without a texture reads white, so the color is the tint.
        const auto green = draw(tintedID);
        EXPECT_EQ(green[0], 0);
        EXPECT_EQ(green[1], 255);
        const auto red = draw(texturedID);
        EXPECT_GT(red[0], 100);
        EXPECT_EQ(red[1], 0);
    }

    TEST_P(RenderAssetCacheGPU, ShaderOutsideTheMaterialContractIsNotCompiled)
    {
        ShaderData shader;
        shader.Code = CompileTestMaterialShader(TestMaterialVertex, R"(#version 450
layout(set = 3, binding = 0) uniform sampler2D other;
layout(location = 0) out vec4 color;
void main() { color = texture(other, vec2(0)); }
)");
        auto created = GpuShader::Create(shader);
        ASSERT_TRUE(std::holds_alternative<AssetError>(created));
        EXPECT_NE(std::get<AssetError>(created).Message.find("set 3"), std::string::npos);
        EXPECT_TRUE(std::holds_alternative<GpuShader>(GpuShader::Create({CompileTestMaterialShader(), {}, {}})));
        shader.Code             = CompileTestMaterialShader();
        shader.TextureFallbacks = {{"Typo", ShaderTextureFallback::Normal}};
        // A typo must fail instead of silently giving the sampler its default white fallback.
        EXPECT_TRUE(std::holds_alternative<AssetError>(GpuShader::Create(shader)));
    }

    // The editor's sample products create from the same bytecode on each backend.
    TEST_P(RenderAssetCacheGPU, EditorSampleShaderProductsCreateOnBothBackends)
    {
        const auto read = [](const std::string& name) {
            std::ifstream     file(std::string(KANS_TEST_EDITOR_ASSET_DIR) + "/shaders/" + name);
            std::stringstream text;
            text << file.rdbuf();
            return text.str();
        };
        for (const std::string name : {"Toon/Toon", "ToneCharacter/ToneCharacter"})
        {
            ShaderData shader;
            shader.Code  = CompileTestMaterialShader(read(name + ".vert"), read(name + ".frag"));
            auto created = GpuShader::Create(shader);
            EXPECT_TRUE(std::holds_alternative<GpuShader>(created))
                << name << ": " << std::get<AssetError>(created).Message;
        }
    }

    TEST_P(RenderAssetCacheGPU, SceneSubmissionUsesStandardAndCustomShadersInLitDebugAndPreviewPasses)
    {
        // Exercise SubmitModel itself: debug override and engine frame upload cannot be bypassed by a helper test.
        const AssetID modelID {Source, 1}, leftID {Source, 2}, rightID {Source, 3}, standardID {Source, 4},
            customID {Source, 5}, shaderID {Source, 6};
        ShaderData shader;
        shader.Code = CompileTestMaterialShader();
        MaterialSource custom;
        custom.Shader     = shaderID;
        custom.Parameters = {{"Tint", {0, 1, 0, 1}}, {"Strength", {1}}};
        MaterialSource standard;
        standard.Parameters["BaseColor"] = {1, 0, 0, 1};
        auto     customData              = std::get<MaterialData>(ResolveMaterialSource(custom, &shader));
        auto     standardData            = std::get<MaterialData>(ResolveMaterialSource(standard, nullptr));
        MeshData left;
        for (auto [x, y] : {std::pair {-1.0f, -1.0f}, {0.0f, -1.0f}, {0.0f, 1.0f}, {-1.0f, 1.0f}})
        {
            MeshVertexData vertex;
            vertex.Position = {x, y, 0};
            vertex.Normal   = {0, 0, 1};
            vertex.Tangent  = {1, 0, 0, 1};
            left.Vertices.push_back(vertex);
        }
        left.Indices   = {0, 1, 2, 2, 3, 0};
        left.Material  = standardID;
        auto right     = left;
        right.Material = customID;
        for (auto& vertex : right.Vertices)
            vertex.Position[0] += 1;
        ModelData model;
        model.Nodes.push_back({"Root", -1, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, {leftID, rightID}});
        AssetManager assets(
            Provider,
            Registry({Store(modelID, AssetType::Model, std::get<std::vector<std::byte>>(EncodeModelProduct(model))),
                      Store(leftID, AssetType::StaticMesh, std::get<std::vector<std::byte>>(EncodeMeshProduct(left))),
                      Store(rightID, AssetType::StaticMesh, std::get<std::vector<std::byte>>(EncodeMeshProduct(right))),
                      Store(standardID,
                            AssetType::Material,
                            std::get<std::vector<std::byte>>(EncodeMaterialProduct(standardData))),
                      Store(customID,
                            AssetType::Material,
                            std::get<std::vector<std::byte>>(EncodeMaterialProduct(customData))),
                      Store(shaderID,
                            AssetType::Shader,
                            std::get<std::vector<std::byte>>(EncodeShaderProduct(shader.Code)))}));
        auto        scene = CreateRef<Scene>();
        RenderScene renderScene(scene);
        renderScene.OnViewportResize(16, 16);
        SceneRenderer renderer(&renderScene);
        RegisterDefaultEditorFeatures(renderer.GetFeatures());
        renderer.SetAssetManager(&assets);
        auto& cache = *renderer.GetAssetCache();
        ASSERT_TRUE(Frames(cache, [&] {
            return cache.GetModel(modelID) && cache.GetMesh(leftID) && cache.GetMesh(rightID) &&
                   cache.GetMaterialBindings(standardID) && cache.GetMaterialBindings(customID)->Shader == shaderID;
        }));
        SceneInfo info {};
        info.sceneCamera.camera           = Camera(glm::mat4(1));
        info.sceneCamera.viewMatrix       = glm::mat4(1);
        info.sceneCamera.Position         = {0, 0, 2};
        info.pointLight.Position          = {0, 0, 1};
        info.pointLight.Ambient_Intensity = {2, 2, 2};
        ProductModelComponent component;
        component.Model = modelID;
        if (TestBackend() == GpuTestBackend::OpenGL)
        {
            glDisable(GL_CULL_FACE);
            glDisable(GL_BLEND);
            glEnable(GL_DEPTH_TEST);
            glViewport(0, 0, 16, 16);
        }
        const auto read = [&](int x) {
            // Model submissions are consumed at EndScene; inspect the scene texture after unbinding it.
            auto                   image = ReadImage(renderer.GetRenderTarget().Colors[0].Target);
            std::array<uint8_t, 4> pixel {};
            std::copy_n(image.data() + (8 * 16 + x) * 4, 4, pixel.data());
            return pixel;
        };
        renderer.BeginScene(info);
        renderer.SubmitModel(component, glm::mat4(1));
        renderer.EndScene();
        EXPECT_GT(read(4)[0], 150);
        EXPECT_EQ(read(12)[1], 255); // Custom shader still runs in the lit view.
        EXPECT_EQ(read(12)[0], 0);
        EXPECT_EQ(renderer.GetStats().DrawCalls, 2u);
        EXPECT_EQ(renderer.GetStats().Triangles, 4u);
        renderer.SetViewMode(SceneViewMode::BaseColor);
        renderer.BeginScene(info);
        renderer.SubmitModel(component, glm::mat4(1));
        renderer.EndScene();
        EXPECT_EQ(read(4), (std::array<uint8_t, 4> {255, 0, 0, 255}));
        EXPECT_EQ(read(12), (std::array<uint8_t, 4> {255, 255, 255, 255}));
        renderer.SetViewMode(SceneViewMode::Normals);
        renderer.BeginScene(info);
        renderer.SubmitModel(component, glm::mat4(1));
        renderer.EndScene();
        for (int x : {4, 12})
        {
            const auto pixel = read(x);
            EXPECT_NEAR(pixel[0], 128, 1); // RGBA8 quantization of 0.5.
            EXPECT_NEAR(pixel[1], 128, 1);
            EXPECT_EQ(pixel[2], 255);
        }
        renderer.BeginScene(info);
        renderer.SubmitModel(component, glm::mat4(1));
        renderer.SetPreview(component, glm::translate(glm::mat4(1), {0, 0, -0.1f}));
        renderer.EndScene();
        EXPECT_EQ(renderer.GetStats().DrawCalls, 4u); // Preview submits both parts through the lit path.
        // Lit green plus the existing preview fill: .88 * (0,1,0) + .12 * (.35,.7,1).
        EXPECT_EQ(read(12), (std::array<uint8_t, 4> {11, 246, 31, 255}));
        renderer.SetViewMode(SceneViewMode::Wireframe);
        renderer.BeginScene(info);
        renderer.SubmitModel(component, glm::mat4(1));
        renderer.EndScene();
        if (TestBackend() == GpuTestBackend::OpenGL)
        {
            NativeUIFrame ui;
            GLint         polygon[2] {};
            // ImGui must establish filled drawing after a wireframe RHI pass; it restores wireframe afterwards.
            ImGui::GetBackgroundDrawList()->AddCallback(
                [](const ImDrawList*, const ImDrawCmd* command) {
                    glGetIntegerv(GL_POLYGON_MODE, static_cast<GLint*>(command->UserCallbackData));
                },
                polygon);
            ui.Draw();
            EXPECT_EQ(polygon[0], GLint(GL_FILL));
        }
        EXPECT_LT(read(2)[0], 50); // Interior away from the triangulation diagonal stays clear.
        std::vector<MeshDraw> draws;
        renderer.AppendModelDraws(component, glm::mat4(1), 7, draws);
        ASSERT_EQ(draws.size(), 2u);
        EXPECT_EQ(renderer.Pick(draws, glm::mat4(1), {4, 8}), 7u);
        if (TestBackend() == GpuTestBackend::OpenGL)
            EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
    }

    TEST_P(RenderAssetCacheGPU, ImportedRemappedModelDrawsAuthoredSlotAndPartOverrides)
    {
        // Catches losing the authored slot during GPU upload or ignoring it in SubmitModel.
        namespace fs    = std::filesystem;
        const auto root = fs::temp_directory_path() / ("kans-remap-gl-" + SourceAssetID::Generate().ToString());
        struct Cleanup
        {
            fs::path Root;
            ~Cleanup()
            {
                std::error_code ignored;
                fs::remove_all(Root, ignored);
            }
        } cleanup {root};
        fs::create_directories(root / "assets");
        std::ofstream(root / "assets/model.mtl") << "newmtl Slot\nKd 1 1 1\n";
        std::ofstream(root / "assets/model.obj") << "mtllib model.mtl\nv -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\n"
                                                    "o Quad\nusemtl Slot\nf 1 2 3\nf 3 4 1\n";
        for (const auto& [name, color] : std::map<std::string, std::vector<double>> {
                 {"bound", {1, 0, 0, 1}}, {"slot", {0, 1, 0, 1}}, {"part", {0, 0, 1, 1}}})
        {
            MaterialSource source;
            source.Name                    = name;
            source.Parameters["BaseColor"] = color;
            std::ofstream(root / "assets" / (name + ".material")) << SerializeMaterialSource(source);
        }
        ProjectConfig config;
        config.Name          = "Override GL";
        config.RootDirectory = root;
        auto opened          = Project::Open(config);
        ASSERT_TRUE(std::holds_alternative<Scope<Project>>(opened));
        auto       project     = std::get<Scope<Project>>(std::move(opened));
        const auto modelSource = project->GetSourceAssetDatabase().FindIDByPath(AssetPath("model.obj"));
        auto       imported    = project->BuildAsset(modelSource, {});
        ASSERT_TRUE(std::holds_alternative<AssetBuildGraphResult>(imported));
        const auto modelID =
            AssetID(modelSource, std::get<AssetBuildGraphResult>(imported).Root.LocalIDs.at("model:root"));
        std::map<std::string, AssetID> materials;
        for (const std::string name : {"bound", "slot", "part"})
        {
            const auto source = project->GetSourceAssetDatabase().FindIDByPath(AssetPath(name + ".material"));
            auto       built  = project->BuildAsset(source, {});
            ASSERT_TRUE(std::holds_alternative<AssetBuildGraphResult>(built));
            materials[name] = AssetID(source, std::get<AssetBuildGraphResult>(built).Root.LocalIDs.at("material:root"));
        }
        auto described = project->GetModelMaterialSlots(modelSource);
        ASSERT_TRUE(std::holds_alternative<std::vector<ModelMaterialSlot>>(described));
        const auto& slots = std::get<std::vector<ModelMaterialSlot>>(described);
        const auto  slot =
            std::find_if(slots.begin(), slots.end(), [](const auto& item) { return item.DisplayName == "Slot"; });
        ASSERT_NE(slot, slots.end());
        ASSERT_TRUE(std::holds_alternative<std::monostate>(
            project->SetModelMaterialRemap(modelSource, slot->Key, materials.at("bound"))));
        ASSERT_TRUE(std::holds_alternative<AssetBuildGraphResult>(project->BuildAsset(modelSource, {})));
        AssetID meshID;
        for (const auto& record : project->GetRuntimeAssetRegistry()->GetAllProducts())
            if (record.Metadata.assetID.GetSourceID() == modelSource && record.Metadata.Type == AssetType::StaticMesh)
                meshID = record.Metadata.assetID;
        ASSERT_TRUE(meshID.IsValid());
        auto provider = LooseFileAssetProvider::Open(project->GetProductDirectory());
        ASSERT_TRUE(std::holds_alternative<Ref<LooseFileAssetProvider>>(provider));
        AssetManager assets(std::get<Ref<LooseFileAssetProvider>>(provider), project->GetRuntimeAssetRegistry());
        auto         scene = CreateRef<Scene>();
        RenderScene  renderScene(scene);
        renderScene.OnViewportResize(16, 16);
        SceneRenderer renderer(&renderScene);
        RegisterDefaultEditorFeatures(renderer.GetFeatures());
        renderer.SetAssetManager(&assets);
        auto& cache = *renderer.GetAssetCache();
        ASSERT_TRUE(Frames(cache, [&] {
            return cache.GetModel(modelID) && cache.GetMesh(meshID) &&
                   cache.GetMaterialBindings(materials.at("bound")) &&
                   cache.GetMaterialBindings(materials.at("slot")) && cache.GetMaterialBindings(materials.at("part"));
        }));
        SceneInfo info {};
        info.sceneCamera.camera     = Camera(glm::mat4(1));
        info.sceneCamera.viewMatrix = glm::mat4(1);
        info.sceneCamera.Position   = {0, 0, 2};
        renderer.SetViewMode(SceneViewMode::BaseColor);
        ProductModelComponent component;
        component.Model = modelID;
        const auto draw = [&] {
            renderer.BeginScene(info);
            renderer.SubmitModel(component, glm::mat4(1));
            renderer.EndScene();
            auto                   pixels = ReadImage(renderer.GetRenderTarget().Colors[0].Target);
            std::array<uint8_t, 4> pixel {};
            std::copy_n(pixels.data() + (8 * 16 + 8) * 4, 4, pixel.data());
            return pixel;
        };
        EXPECT_EQ(draw(), (std::array<uint8_t, 4> {255, 0, 0, 255}));
        component.SetOverride(slot->EmbeddedMaterial, materials.at("slot"));
        EXPECT_EQ(draw(), (std::array<uint8_t, 4> {0, 255, 0, 255}));
        component.SetOverride(meshID, materials.at("part"));
        EXPECT_EQ(draw(), (std::array<uint8_t, 4> {0, 0, 255, 255}));
        if (TestBackend() == GpuTestBackend::OpenGL)
            EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
    }

    class MaterialSceneGL : public RenderAssetCacheGL, public testing::WithParamInterface<GpuTestBackend>
    {
    protected:
        GpuTestBackend TestBackend() const override { return GetParam(); }
        void SetUp() override
        {
            ASSERT_NO_FATAL_FAILURE(RenderAssetCacheGL::SetUp());
            Assets     = CreateScope<AssetManager>(Provider, Registry({}));
            LogicScene = CreateRef<Scene>();
            Render     = CreateScope<RenderScene>(LogicScene);
            Render->OnViewportResize(16, 16);
            Renderer = CreateRef<SceneRenderer>(Render.get());
            RegisterDefaultEditorFeatures(Renderer->GetFeatures());
            Renderer->SetAssetManager(Assets.get());
            Info.sceneCamera.camera           = Camera(glm::mat4(1));
            Info.sceneCamera.viewMatrix       = glm::mat4(1);
            Info.sceneCamera.Position         = {0, 0, -2};
            Info.pointLight.Position          = {0, 0, -1};
            Info.pointLight.Ambient_Intensity = {2, 2, 2};
            Custom.Code                       = CompileTestMaterialShader(TestMaterialVertex, R"(#version 450
#include <Kans3D/Alpha.glsl>
layout(set = 1, binding = 0, std140) uniform Parameters { vec4 Tint; } p;
layout(location = 0) out vec4 o_Color;
void main() { KansAlphaClip(p.Tint.a); o_Color = vec4(p.Tint.rgb, KansOutputAlpha(p.Tint.a)); }
)");
            ShaderID                          = Add(AssetType::Shader, EncodeShaderProduct(Custom.Code));
            if (TestBackend() == GpuTestBackend::OpenGL)
                glDisable(GL_FRAMEBUFFER_SRGB);
        }
        void TearDown() override
        {
            Renderer.reset();
            Render.reset();
            LogicScene.reset();
            Assets.reset();
            RenderAssetCacheGL::TearDown();
        }
        AssetID Add(AssetType type, std::variant<std::vector<std::byte>, AssetError> encoded)
        {
            if (const auto* error = std::get_if<AssetError>(&encoded))
                throw std::runtime_error(error->Message);
            AssetID id {Source, ++NextID};
            Records.push_back(Store(id, type, std::get<std::vector<std::byte>>(std::move(encoded))));
            auto result = Assets->SetRegistry(Registry(Records));
            if (const auto* error = std::get_if<AssetError>(&result))
                throw std::runtime_error(error->Message);
            return id;
        }
        AssetID Material(MaterialSource source, bool custom = true)
        {
            if (custom)
                source.Shader = ShaderID;
            auto resolved = ResolveMaterialSource(source, custom ? &Custom : nullptr);
            if (const auto* error = std::get_if<AssetError>(&resolved))
                throw std::runtime_error(error->Message);
            const auto id = Add(AssetType::Material, EncodeMaterialProduct(std::get<MaterialData>(resolved)));
            Materials.push_back(id);
            return id;
        }
        AssetID
        Color(std::array<double, 4> color, MaterialAlphaMode mode = MaterialAlphaMode::Opaque, bool doubleSided = false)
        {
            MaterialSource source;
            source.Parameters["Tint"] = {color.begin(), color.end()};
            source.Render.AlphaMode   = mode;
            source.Render.DoubleSided = doubleSided;
            return Material(source);
        }
        ProductModelComponent
        Quad(AssetID material, float z = 0, bool back = false, float left = -1, float right = 1, float vertexAlpha = 1)
        {
            MeshData mesh;
            mesh.Material = material;
            for (const auto& xy : {glm::vec2 {left, -1}, {right, -1}, {right, 1}, {left, 1}})
            {
                MeshVertexData vertex;
                vertex.Position = {xy.x, xy.y, z};
                vertex.Normal   = {0, 0, 1};
                vertex.Tangent  = {1, 0, 0, 1};
                vertex.UV       = {(xy.x - left) / (right - left), (xy.y + 1) / 2};
                vertex.Color    = {1, 1, 1, vertexAlpha};
                mesh.Vertices.push_back(vertex);
            }
            mesh.Indices = back ? std::vector<uint32_t> {2, 1, 0, 0, 3, 2} : std::vector<uint32_t> {0, 1, 2, 2, 3, 0};
            const auto meshID = Add(AssetType::StaticMesh, EncodeMeshProduct(mesh));
            Meshes.push_back(meshID);
            ModelData model;
            model.Nodes.push_back({"Root", -1, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, {meshID}});
            ProductModelComponent component;
            component.Model = Add(AssetType::Model, EncodeModelProduct(model));
            Models.push_back(component.Model);
            return component;
        }
        void Ready()
        {
            auto& cache = *Renderer->GetAssetCache();
            ASSERT_TRUE(Frames(cache, [&] {
                bool loaded = cache.GetShader(ShaderID) != nullptr;
                for (auto id : Models)
                    loaded &= cache.GetModel(id) != nullptr;
                for (auto id : Meshes)
                    loaded &= cache.GetMesh(id) != nullptr;
                for (auto id : Materials)
                    loaded &= cache.GetMaterialBindings(id) != nullptr;
                return loaded;
            }));
        }
        std::vector<uint8_t> ReadImage(const RenderHardware::Handle<RenderHardware::Texture>& texture)
        {
            auto device = Renderer->GetDevice();
            auto buffer = device->CreateBuffer(
                {16 * 16 * 4, RenderHardware::BufferUsage::CopyDestination, RenderHardware::MemoryUsage::Readback});
            auto commands = device->CreateCommandList();
            commands->CopyTextureToBuffer(texture, {}, buffer);
            device->Submit(commands);
            device->WaitIdle();
            return buffer->Read(0, 16 * 16 * 4);
        }
        std::array<uint8_t, 4> Pixel(int x = 8, int y = 8)
        {
            auto image = ReadImage(Renderer->GetRenderTarget().Colors[0].Target);
            y          = DeviceRow(y, 16, Renderer->GetDevice()->GetCoordinateConventions());
            std::array<uint8_t, 4> pixel;
            std::copy_n(image.data() + (y * 16 + x) * 4, 4, pixel.data());
            return pixel;
        }
        float Depth(int x = 8)
        {
            auto     depth = ReadImage(Renderer->GetRenderTarget().Depth.Target);
            uint32_t word;
            auto     y = DeviceRow(8, 16, Renderer->GetDevice()->GetCoordinateConventions());
            std::memcpy(&word, depth.data() + (y * 16 + x) * 4, 4);
            return float(word >> 8) / 16777215.0f;
        }
        Ref<Scene>                 LogicScene;
        Scope<RenderScene>         Render;
        Ref<SceneRenderer>         Renderer;
        Scope<AssetManager>        Assets;
        SceneInfo                  Info {};
        ShaderData                 Custom;
        AssetID                    ShaderID;
        uint64_t                   NextID = 0;
        std::vector<ProductRecord> Records;
        std::vector<AssetID>       Models, Meshes, Materials;
    };

    class MaterialSceneNativeGL : public MaterialSceneGL
    {
    protected:
        GpuTestBackend TestBackend() const override { return GpuTestBackend::OpenGL; }
    };
    class MultiPassSceneGL : public MaterialSceneGL
    {
    protected:
        static constexpr uint32_t Size = 128;
        void                      SetUp() override
        {
            MaterialSceneGL::SetUp();
            Render->OnViewportResize(Size, Size);
            Renderer->SetViewportSize(Size, Size);
            Info.sceneCamera.camera     = Camera(glm::ortho(-1.f, 1.f, -1.f, 1.f, .1f, 10.f));
            Info.sceneCamera.viewMatrix = glm::lookAt(glm::vec3(0, 0, 3), glm::vec3(0), glm::vec3(0, 1, 0));
            Info.sceneCamera.Position   = {0, 0, 3};
            Custom                      = MultiPassShader();
            ShaderID = Add(AssetType::Shader,
                           EncodeShaderProduct(Custom.Code, Custom.ParameterDefaults, {}, {}, {}, Custom.Passes));
        }
        ProductModelComponent Sphere(AssetID material)
        {
            MeshData mesh;
            mesh.Material                = material;
            constexpr uint32_t latitudes = 48, longitudes = 96;
            for (uint32_t i = 0; i <= latitudes; ++i)
                for (uint32_t j = 0; j <= longitudes; ++j)
                {
                    const float    theta = glm::pi<float>() * float(i) / latitudes;
                    const float    phi   = 2 * glm::pi<float>() * float(j) / longitudes;
                    MeshVertexData vertex;
                    vertex.Position = {
                        std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta)};
                    vertex.Normal  = vertex.Position;
                    vertex.Color   = {1, 1, 1, 1};
                    vertex.Tangent = {1, 0, 0, 1};
                    mesh.Vertices.push_back(vertex);
                }
            for (uint32_t i = 0; i < latitudes; ++i)
                for (uint32_t j = 0; j < longitudes; ++j)
                {
                    auto a = i * (longitudes + 1) + j, b = a + longitudes + 1;
                    mesh.Indices.insert(mesh.Indices.end(), {a, b, b + 1, a, b + 1, a + 1});
                }
            const auto meshID = Add(AssetType::StaticMesh, EncodeMeshProduct(mesh));
            Meshes.push_back(meshID);
            ModelData model;
            model.Nodes.push_back({"Root", -1, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, {meshID}});
            ProductModelComponent component;
            component.Model = Add(AssetType::Model, EncodeModelProduct(model));
            Models.push_back(component.Model);
            return component;
        }
        std::vector<uint8_t> Image(uint32_t attachment = 0)
        {
            using namespace RenderHardware;
            auto texture = attachment == 2 ? Renderer->GetRenderTarget().Depth.Target :
                                             Renderer->GetRenderTarget().Colors.at(attachment).Target;
            auto device  = Renderer->GetDevice();
            auto buffer  = device->CreateBuffer({Size * Size * 4, BufferUsage::CopyDestination, MemoryUsage::Readback});
            auto commands = device->CreateCommandList();
            commands->CopyTextureToBuffer(texture, {}, buffer, 0);
            device->Submit(commands);
            device->WaitIdle();
            auto bytes = buffer->Read(0, Size * Size * 4);
            if (device->GetCoordinateConventions().Origin == RenderHardware::FramebufferOrigin::UpperLeft)
                for (uint32_t y = 0; y < Size / 2; ++y)
                    std::swap_ranges(bytes.begin() + y * Size * 4,
                                     bytes.begin() + (y + 1) * Size * 4,
                                     bytes.begin() + (Size - 1 - y) * Size * 4);
            return bytes;
        }
        static std::array<uint8_t, 4> At(const std::vector<uint8_t>& image, uint32_t x, uint32_t y)
        {
            std::array<uint8_t, 4> pixel;
            for (size_t i = 0; i < 4; ++i)
                pixel[i] = image.at((y * Size + x) * 4 + i);
            return pixel;
        }
        static glm::mat4 Transform(float x, float y = 0, float z = 0)
        {
            return glm::scale(glm::translate(glm::mat4(1), {x, y, z}), {.28f, .28f, .28f});
        }
        size_t TagPasses() const
        {
            size_t      count = 0;
            const auto& graph = Renderer->GetRenderGraph();
            for (auto pass : graph.ExecutionOrder())
                count += graph.PassName(pass) == "Material passes: Outline";
            return count;
        }
    };

    TEST_P(MultiPassSceneGL, InvertedHullSphereHasExteriorRingAndExactUnchangedInteriorAndDepth)
    {
        // Bugs: wrong cull/depth/write state, outline before opaque, lost material width or object transform/normal
        // matrix.
        const auto left  = Sphere(Color({1, 0, 0, 1}));
        const auto right = Sphere(Color({0, 1, 0, 1}));
        Ready();
        auto draw = [&] {
            Renderer->BeginScene(Info);
            Renderer->SubmitModel(left, glm::rotate(Transform(-.45f, .12f), glm::half_pi<float>(), glm::vec3(0, 0, 1)));
            Renderer->SubmitModel(right, Transform(.45f, -.12f));
            Renderer->EndScene();
        };
        Renderer->GetFeatures().SetEnabled("MaterialOutline", false);
        draw();
        const auto baseline = Image(), baselineDepth = Image(2);
        const auto materialGroup = Renderer->GetAssetCache()->GetMaterialBindings(Materials.front())->Group;
        ASSERT_NE(materialGroup, nullptr);
        EXPECT_EQ(TagPasses(), 0u);
        EXPECT_EQ(Renderer->GetStats().DrawCalls, 2u);
        Renderer->GetFeatures().SetEnabled("MaterialOutline", true);
        draw();
        const auto outlined = Image();
        EXPECT_EQ(Renderer->GetAssetCache()->GetMaterialBindings(Materials.front())->Group, materialGroup);
        EXPECT_EQ(Image(2), baselineDepth); // depth_write=off, exact D24S8 bytes
        ASSERT_EQ(TagPasses(), 1u);
        EXPECT_EQ(Renderer->GetStats().DrawCalls, 4u);
        size_t red = 0, green = 0, ring = 0;
        for (uint32_t y = 0; y < Size; ++y)
            for (uint32_t x = 0; x < Size; ++x)
            {
                auto before = At(baseline, x, y), after = At(outlined, x, y);
                if (before[0] == 255 || before[1] == 255)
                {
                    EXPECT_EQ(after, before) << x << "," << y;
                    red += before[0] == 255;
                    green += before[1] == 255;
                }
                else if (after != before)
                {
                    EXPECT_EQ(before, (std::array<uint8_t, 4> {5, 5, 5, 255}));
                    EXPECT_EQ(after, (std::array<uint8_t, 4> {0, 0, 0, 255}));
                    const float px = 2.f * (x + .5f) / Size - 1, py = 2.f * (y + .5f) / Size - 1;
                    const float distance = std::min(glm::length(glm::vec2(px + .45f, py - .12f)),
                                                    glm::length(glm::vec2(px - .45f, py + .12f)));
                    EXPECT_GT(distance, .275f); // outside the polygonal surface; 0.005 allowance for tessellation
                    EXPECT_LT(distance, .365f); // .28 radius + .08 width, plus 0.005 tessellation/pixel allowance
                    ++ring;
                }
            }
        EXPECT_GT(red, 950u);
        EXPECT_GT(green, 950u);
        EXPECT_GT(ring, 1200u);
        EXPECT_LT(ring, 1450u); // analytic two annuli ~1318 pixels, polygon silhouette allowance
        const auto& graph    = Renderer->GetRenderGraph();
        auto        material = std::find_if(graph.ExecutionOrder().begin(), graph.ExecutionOrder().end(), [&](auto p) {
            return graph.PassName(p) == "Materials (opaque/mask, blend)";
        });
        auto        outline  = std::find_if(graph.ExecutionOrder().begin(), graph.ExecutionOrder().end(), [&](auto p) {
            return graph.PassName(p) == "Material passes: Outline";
        });
        ASSERT_NE(material, graph.ExecutionOrder().end());
        ASSERT_NE(outline, graph.ExecutionOrder().end());
        EXPECT_LT(material, outline); // independently catches pre-opaque injection even if pixels happen to match
        const auto* bindings = Renderer->GetAssetCache()->GetMaterialBindings(Materials.front());
        const auto* shader   = Renderer->GetAssetCache()->GetShader(bindings->Shader);
        ASSERT_EQ(shader->Passes.size(), 1u);
        ASSERT_EQ(shader->Passes[0].Pipelines.size(), 1u);
        const auto& state = shader->Passes[0].Pipelines.begin()->second->GetDesc();
        EXPECT_EQ(state.GroupLayouts,
                  (std::vector<RenderHardware::Handle<RenderHardware::BindGroupLayout>> {shader->EngineLayout,
                                                                                         shader->MaterialLayout}));
        EXPECT_EQ(state.Rasterizer.Cull, RenderHardware::CullMode::Front);
        EXPECT_TRUE(state.DepthStencil.DepthTest);
        EXPECT_FALSE(state.DepthStencil.DepthWrite);
        EXPECT_EQ(state.DepthStencil.DepthCompare, RenderHardware::CompareOp::LessEqual);
        EXPECT_FALSE(state.Blends[0].Enabled);
        EXPECT_EQ(state.Blends[0].WriteMask, 7u);
    }

    TEST_P(MultiPassSceneGL, ForegroundOpaqueOccludesHullAndUnTaggedMaterialsAddNoPass)
    {
        // Bugs: no tag filter, missing depth test, or feature draws cached but unsubmitted models.
        auto       tagged       = Sphere(Color({1, 0, 0, 1}));
        const auto taggedShader = ShaderID;
        auto       single       = Custom;
        single.Passes.clear();
        ShaderID      = Add(AssetType::Shader, EncodeShaderProduct(single.Code));
        auto untagged = Sphere(Color({0, 1, 0, 1}));
        auto occluder = Quad(Color({0, 0, 1, 1}), .6f, false, -.16f, .16f);
        Ready();
        Renderer->BeginScene(Info);
        Renderer->SubmitModel(untagged, Transform(.45f));
        Renderer->EndScene();
        EXPECT_EQ(TagPasses(), 0u);
        EXPECT_EQ(Renderer->GetStats().DrawCalls, 1u);
        const auto alone = Image();
        EXPECT_EQ(At(alone, 116, 64), (std::array<uint8_t, 4> {5, 5, 5, 255}));
        auto draw = [&] {
            Renderer->BeginScene(Info);
            Renderer->SubmitModel(tagged, Transform(-.45f));
            Renderer->SubmitModel(untagged, Transform(.45f));
            Renderer->SubmitModel(occluder, glm::translate(glm::mat4(1), {-.13f, 0, 0}));
            Renderer->EndScene();
        };
        Renderer->GetFeatures().SetEnabled("MaterialOutline", false);
        draw();
        const auto baseline = Image();
        Renderer->GetFeatures().SetEnabled("MaterialOutline", true);
        draw();
        const auto outlined = Image();
        EXPECT_EQ(Renderer->GetStats().DrawCalls, 4u); // exactly three surfaces, only one shell
        EXPECT_EQ(TagPasses(), 1u);
        size_t occluded = 0, ring = 0;
        for (uint32_t y = 0; y < Size; ++y)
            for (uint32_t x = 0; x < Size; ++x)
            {
                const auto before = At(baseline, x, y), after = At(outlined, x, y);
                if (before[2] == 255)
                {
                    EXPECT_EQ(after, before);
                    ++occluded;
                }
                if (x >= Size / 2)
                    EXPECT_EQ(after, before); // no outline around the untagged sphere
                ring += after != before;
            }
        EXPECT_GT(occluded, 2000u);
        EXPECT_GT(ring, 300u);
        auto* shader = Renderer->GetAssetCache()->GetShader(taggedShader);
        ASSERT_NE(shader, nullptr);
        EXPECT_FALSE(shader->Passes[0].Pipelines.empty());
    }

    TEST_P(MultiPassSceneGL, FeatureHonorsPreparedOrderAndUnreadyDrawsAtConfigurablePoint)
    {
        // Bug: feature scans View.Draws or cache contents and reintroduces draws culled by a pipeline.
        auto left = Sphere(Color({1, 0, 0, 1})), right = Sphere(Color({0, 1, 0, 1}));
        Ready();
        Renderer->GetFeatures().SetEnabled("MaterialOutline", false);
        Renderer->BeginScene(Info);
        Renderer->SubmitModel(left, Transform(-.45f));
        Renderer->SubmitModel(right, Transform(.45f));
        Renderer->EndScene();
        const auto baseline = Image();
        RenderView view     = Renderer->GetRenderView();
        ASSERT_EQ(view.Draws.size(), 2u);
        ASSERT_EQ(view.MaterialOrder.size(), 2u);
        view.MaterialOrder.erase(view.MaterialOrder.begin() + 1); // the second ready draw is culled by this pipeline
        auto                  device = Renderer->GetDevice();
        RenderGraph           graph;
        std::vector<GraphUse> target;
        for (auto color : Renderer->GetRenderTarget().Colors)
        {
            auto resource = graph.ImportTexture("color", color.Target);
            target.push_back({resource, GraphAccess::ColorReadWrite});
            graph.ExportTexture(resource);
        }
        const auto depth = graph.ImportTexture("depth", Renderer->GetRenderTarget().Depth.Target);
        target.push_back({depth, GraphAccess::DepthReadWrite});
        graph.ExportTexture(depth);
        MaterialPassFeature feature("Outline", RenderExtensionPoint::BeforePostProcess);
        EXPECT_TRUE(feature.RunsAt(RenderExtensionPoint::BeforePostProcess));
        EXPECT_FALSE(feature.RunsAt(RenderExtensionPoint::BeforeOpaque));
        RenderFeatureContext context {graph, view, target, Renderer.get()};
        feature.AddPasses(RenderExtensionPoint::BeforePostProcess, context);
        ASSERT_EQ(graph.PassCount(), 1u);
        graph.Compile();
        TransientTexturePool pool;
        graph.Execute(*device, pool);
        const auto outlined = Image();
        EXPECT_EQ(Renderer->GetStats().DrawCalls, 3u);
        size_t ring = 0;
        for (uint32_t y = 0; y < Size; ++y)
            for (uint32_t x = 0; x < Size; ++x)
            {
                if (x >= Size / 2)
                    EXPECT_EQ(At(outlined, x, y), At(baseline, x, y));
                else
                    ring += At(outlined, x, y) != At(baseline, x, y);
            }
        EXPECT_GT(ring, 600u);
        view.Draws[0].Ready = false; // stale order entry must not make an unavailable/invisible draw visible
        RenderGraph          unready;
        RenderFeatureContext none {unready, view, target, Renderer.get()};
        feature.AddPasses(RenderExtensionPoint::BeforePostProcess, none);
        EXPECT_EQ(unready.PassCount(), 0u);
        view.MaterialOrder.clear();
        feature.AddPasses(RenderExtensionPoint::BeforePostProcess, none);
        EXPECT_EQ(unready.PassCount(), 0u);
    }

    TEST_P(MultiPassSceneGL, ExtraPassReceivesEachObjectsMaterialRenderDataAndOnlyMatchingTagsRun)
    {
        // Bug: the extra pass gets an old/short object block or the previous object's alpha mode/cutoff/double-sided
        // values.
        Custom.Passes[0].Code.Modules[1].Words = CompileTestStage(R"(#version 450
#include <Kans3D/Engine.glsl>
layout(location=0) out vec4 Color;
void main() { Color = vec4(u_Object.MaterialRender.y, u_Object.MaterialRender.z, u_Object.MaterialRender.x / 2, 1); }
)",
                                                                  shaderc_fragment_shader);
        // A second tag must remain inactive without its own feature.
        auto shadow = Custom.Passes[0];
        shadow.Name = "Shadow";
        shadow.Tag  = "ShadowCaster";
        Custom.Passes.push_back(shadow);
        ShaderID = Add(AssetType::Shader, EncodeShaderProduct(Custom.Code, {}, {}, {}, {}, Custom.Passes));
        MaterialSource first;
        first.Render.AlphaMode           = MaterialAlphaMode::Mask;
        first.Render.AlphaCutoff         = .25f;
        first.Render.DoubleSided         = true;
        first.Parameters["Tint"]         = {1, 0, 0, 1};
        first.Parameters["OutlineWidth"] = {.08};
        MaterialSource second            = first;
        second.Render.AlphaMode          = MaterialAlphaMode::Opaque;
        second.Render.AlphaCutoff        = .75f;
        second.Render.DoubleSided        = false;
        auto left = Sphere(Material(first)), right = Sphere(Material(second));
        Ready();
        Renderer->BeginScene(Info);
        Renderer->SubmitModel(left, Transform(-.45f));
        Renderer->SubmitModel(right, Transform(.45f));
        Renderer->EndScene();
        EXPECT_EQ(Renderer->GetStats().DrawCalls, 4u);
        auto* shader = Renderer->GetAssetCache()->GetShader(ShaderID);
        ASSERT_NE(shader, nullptr);
        ASSERT_EQ(shader->Passes.size(), 2u);
        EXPECT_FALSE(shader->Passes[0].Pipelines.empty());
        EXPECT_TRUE(shader->Passes[1].Pipelines.empty());
        const auto image = Image();
        // Pixel centres at radius 0.33 sit safely within the .28-.36 shell ring, away from tessellation edges.
        const auto a = At(image, 14, 64), b = At(image, 113, 64);
        EXPECT_EQ(a, (std::array<uint8_t, 4> {64, 255, 128, 255}));
        EXPECT_EQ(b, (std::array<uint8_t, 4> {191, 0, 0, 255}));
        Renderer->GetFeatures().Register(
            "ShadowTest",
            std::make_unique<MaterialPassFeature>("ShadowCaster", RenderExtensionPoint::BeforePostProcess));
        Renderer->BeginScene(Info);
        Renderer->SubmitModel(left, Transform(-.45f));
        Renderer->EndScene();
        EXPECT_EQ(Renderer->GetStats().DrawCalls, 3u); // main + outline + explicitly registered shadow tag
        EXPECT_FALSE(shader->Passes[1].Pipelines.empty());
    }

    TEST_P(MultiPassSceneGL, RuntimeUsesEveryPassStateAndCachesIndependentlyOfMainMaterial)
    {
        // Bug: extra pipelines inherit main cull/depth/blend state, or share its cached pipeline.
        Custom.Passes[0].State = {ShaderCullMode::None,
                                  false,
                                  true,
                                  ShaderDepthCompare::GreaterEqual,
                                  true,
                                  ShaderBlendFactor::SourceAlpha,
                                  ShaderBlendFactor::OneMinusSourceAlpha,
                                  ShaderBlendFactor::DestinationAlpha,
                                  ShaderBlendFactor::OneMinusDestinationAlpha,
                                  9};
        ShaderID   = Add(AssetType::Shader, EncodeShaderProduct(Custom.Code, {}, {}, {}, {}, Custom.Passes));
        auto model = Sphere(Color({1, 0, 0, 1}, MaterialAlphaMode::Blend, true));
        Ready();
        for (unsigned frame = 0; frame < 2; ++frame)
        {
            Renderer->BeginScene(Info);
            Renderer->SubmitModel(model, Transform(0));
            Renderer->EndScene();
        }
        auto* shader = Renderer->GetAssetCache()->GetShader(ShaderID);
        ASSERT_NE(shader, nullptr);
        ASSERT_EQ(shader->Pipelines.size(), 1u);
        ASSERT_EQ(shader->Passes.size(), 1u);
        ASSERT_EQ(shader->Passes[0].Pipelines.size(), 1u);
        auto pipeline = shader->Passes[0].Pipelines.begin()->second;
        EXPECT_NE(pipeline, shader->Pipelines.begin()->second);
        EXPECT_EQ(pipeline->GetDesc().Program, shader->Passes[0].Program);
        const auto& state = pipeline->GetDesc();
        EXPECT_EQ(state.Rasterizer.Cull, RenderHardware::CullMode::None);
        EXPECT_FALSE(state.DepthStencil.DepthTest);
        EXPECT_TRUE(state.DepthStencil.DepthWrite);
        EXPECT_EQ(state.DepthStencil.DepthCompare, RenderHardware::CompareOp::GreaterEqual);
        for (const auto& blend : state.Blends)
        {
            EXPECT_TRUE(blend.Enabled);
            EXPECT_EQ(blend.WriteMask, 9u);
            EXPECT_EQ(blend.SourceColor, RenderHardware::BlendFactor::SourceAlpha);
            EXPECT_EQ(blend.DestinationColor, RenderHardware::BlendFactor::OneMinusSourceAlpha);
            EXPECT_EQ(blend.SourceAlpha, RenderHardware::BlendFactor::DestinationAlpha);
            EXPECT_EQ(blend.DestinationAlpha, RenderHardware::BlendFactor::OneMinusDestinationAlpha);
        }
        // Green/blue masked writes stay exactly as they were in the transparent main surface.
        EXPECT_EQ(At(Image(), 64, 64)[1], 0u);
        EXPECT_EQ(At(Image(), 64, 64)[2], 0u);
    }

    TEST_P(MaterialSceneGL, PerDrawGroupsReusePoolsAndPipelinesAfterWarmup)
    {
        // Bugs: rebuilding a graphics pipeline per draw, creating a descriptor
        // pool per group, or retaining retired per-draw sets until pools grow each frame.
        const auto model = Quad(Color({1, 0, 0, 1}));
        Ready();
        constexpr uint32_t draws = 512;
        auto               frame = [&] {
            Renderer->BeginScene(Info);
            for (uint32_t i = 0; i < draws; ++i)
                Renderer->SubmitModel(model, glm::mat4(1));
            Renderer->EndScene();
            EXPECT_EQ(Renderer->GetStats().DrawCalls, draws);
            EXPECT_EQ(Pixel(), (std::array<uint8_t, 4> {255, 0, 0, 255}));
        };
        frame();
        if (TestBackend() == GpuTestBackend::Vulkan)
        {
            const auto before = RenderHardware::VulkanTestProbes::Inspect(*Renderer->GetDevice());
            for (uint32_t i = 0; i < 3; ++i)
                frame();
            const auto after = RenderHardware::VulkanTestProbes::Inspect(*Renderer->GetDevice());
            EXPECT_EQ(after.PipelineCreations, before.PipelineCreations);
            EXPECT_EQ(after.DescriptorPools, before.DescriptorPools);
            EXPECT_LE(after.DescriptorPools, 2u);
            // Group-0 ranges share one native set per frame; offset changes
            // still select different objects through dynamic offsets.
            EXPECT_EQ(after.DescriptorAllocations - before.DescriptorAllocations, 3u);
            const auto ns = (after.GroupCreationNanoseconds - before.GroupCreationNanoseconds) / (3u * draws);
            RecordProperty("group_creation_ns", std::to_string(ns));
            RecordProperty("descriptor_pools", std::to_string(after.DescriptorPools));
            std::cout << "Per-draw group creation (validation enabled): " << ns << " ns, " << after.DescriptorPools
                      << " pools, pipeline creations after warm-up: "
                      << after.PipelineCreations - before.PipelineCreations << std::endl;
        }
        else
            for (uint32_t i = 0; i < 3; ++i)
                frame();
    }

    TEST_P(MaterialSceneGL, RuntimeGridPreservesLegacyOpaquePixelsAndOccludesSkyAroundModel)
    {
        using namespace RenderHardware;
        constexpr uint32_t size = 64;
        Render->OnViewportResize(size, size);
        Renderer->SetViewportSize(size, size);
        auto  camera     = LogicScene->CreateEntity("runtime camera");
        auto& projection = camera.AddComponent<CameraComponent>().SceneCamera;
        projection.SetPerspectve(glm::radians(45.0f), .1f, 10.0f);
        projection.SetViewportSize(size, size);
        auto& transform                             = camera.GetComponent<TransformComponent>();
        transform.Position                          = {0, 2, 2};
        transform.Rotation                          = {glm::radians(-45.0f), 0, 0};
        auto model                                  = LogicScene->CreateEntity("blue model");
        model.AddComponent<ProductModelComponent>() = Quad(Color({0, 0, 1, 1}, MaterialAlphaMode::Opaque, true));
        auto& modelTransform                        = model.GetComponent<TransformComponent>();
        modelTransform.Position                     = {-.6f, .5f, 0};
        modelTransform.Scale                        = {.3f, .3f, .3f};
        Ready();
        auto resource                          = Render->GetRenderResource();
        resource.Piplinestate.EnableGridShader = true;
        Render->SetRenderResource(resource);
        auto device = Renderer->GetDevice();
        auto read   = [&](Handle<RenderHardware::Texture> target) {
            std::vector<uint8_t> bytes(size * size * 4);
            auto buffer   = device->CreateBuffer({bytes.size(), BufferUsage::CopyDestination, MemoryUsage::Readback});
            auto commands = device->CreateCommandList();
            commands->CopyTextureToBuffer(target, {}, buffer, 0);
            device->Submit(commands);
            device->WaitIdle();
            return buffer->Read(0, bytes.size());
        };
        // Baseline the same runtime model + grid before supplying a sky. This
        // catches blending the editor grid, disabling its depth writes, masking
        // its alpha, or letting a later far-plane sky overwrite its line pixels.
        Render->OnRenderRuntime(Renderer, TimeStep(0));
        auto        target = Renderer->GetRenderTarget();
        auto        before = read(target.Colors[0].Target), beforeDepth = read(target.Depth.Target);
        TextureDesc desc;
        desc.Dimension      = TextureDimension::Cube;
        desc.PixelFormat    = Format::RGBA32Float;
        desc.Usage          = RenderHardware::TextureUsage::Sampled | RenderHardware::TextureUsage::CopyDestination;
        auto            sky = device->CreateTexture(desc);
        const glm::vec4 green(0, 4, 0, 1);
        auto            upload = device->CreateBuffer({sizeof(green), BufferUsage::CopySource, MemoryUsage::Upload});
        upload->Write(0, &green, sizeof(green));
        auto commands = device->CreateCommandList();
        for (uint32_t face = 0; face < 6; ++face)
            commands->CopyBufferToTexture(upload, 0, sky, {0, face});
        device->Submit(commands);
        const_cast<Environment&>(Renderer->GetEnvironment()).IrradianceMap = sky;
        Render->OnRenderRuntime(Renderer, TimeStep(0));
        auto     after = read(target.Colors[0].Target), afterDepth = read(target.Depth.Target);
        uint32_t lines = 0, background = 0, modelPixels = 0;
        for (uint32_t i = 0; i < size * size; ++i)
        {
            std::array<uint8_t, 4> oldPixel, pixel;
            std::copy_n(before.data() + i * 4, 4, oldPixel.data());
            std::copy_n(after.data() + i * 4, 4, pixel.data());
            if (oldPixel == std::array<uint8_t, 4> {230, 230, 230, 128})
            {
                ++lines;
                EXPECT_EQ(pixel, oldPixel) << "grid pixel " << i;
                uint32_t oldWord, word;
                std::memcpy(&oldWord, beforeDepth.data() + i * 4, 4);
                std::memcpy(&word, afterDepth.data() + i * 4, 4);
                EXPECT_LT(word >> 8, 0xffffffu) << "grid must occlude far-plane sky " << i;
                EXPECT_EQ(word >> 8, oldWord >> 8) << "sky changed grid depth " << i;
            }
            else if (oldPixel[0] == 0 && oldPixel[1] == 0 && oldPixel[2] == 255)
            {
                ++modelPixels;
                EXPECT_EQ(pixel, oldPixel) << "sky changed model pixel " << i;
            }
            else
            {
                ++background;
                EXPECT_EQ(oldPixel, (std::array<uint8_t, 4> {5, 5, 5, 255}));
                EXPECT_EQ(pixel, (std::array<uint8_t, 4> {0, 230, 0, 255}));
            }
        }
        EXPECT_GT(lines, 16u);
        EXPECT_GT(background, 100u);
        EXPECT_GT(modelPixels, 16u);
        EXPECT_EQ(Renderer->GetStats().DrawCalls, 1u); // The model really executed.
    }

    TEST_P(MaterialSceneGL, HighlightUsesReimportedMeshAfterQueuedGpuResourceIsDestroyed)
    {
        const auto material = Color({0, 1, 0, 1});
        const auto model    = Quad(material, 0, false, -1, 0);
        Ready();
        auto&                            cache    = *Renderer->GetAssetCache();
        const auto                       meshID   = Meshes.back();
        const std::weak_ptr<RenderHardware::Buffer> oldArray = cache.GetMesh(meshID)->Vertices;

        // The viewport queues the selection before BeginScene, while it still resolves to the left half.
        Renderer->SetHighlight({{model, glm::mat4(1)}});
        Renderer->BeginScene(Info);

        // Republish the same mesh ID on the right half, as a model reimport does.
        MeshData replacement;
        replacement.Material = material;
        for (auto [x, y] : {std::pair {0.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}})
        {
            MeshVertexData vertex;
            vertex.Position = {x, y, 0};
            replacement.Vertices.push_back(vertex);
        }
        replacement.Indices = {0, 1, 2, 2, 3, 0};
        auto record         = std::find_if(
            Records.begin(), Records.end(), [&](const auto& item) { return item.Metadata.assetID == meshID; });
        ASSERT_NE(record, Records.end());
        *record =
            Store(meshID, AssetType::StaticMesh, std::get<std::vector<std::byte>>(EncodeMeshProduct(replacement)));
        ASSERT_TRUE(std::holds_alternative<ProductChangeSet>(Assets->SetRegistry(Registry(Records))));
        ASSERT_TRUE(std::holds_alternative<Ref<const MeshAsset>>(Assets->Load<MeshAsset>(meshID)));
        Renderer->SubmitModel(model, glm::mat4(1)); // Resolves the replacement and destroys the old GpuMesh.
        ASSERT_TRUE(oldArray.expired());            // No retained GPU ownership can hide the dangling pointer.
        ASSERT_EQ(cache.GetMesh(meshID)->LocalBounds.Min.x, 0.0f);
        Renderer->EndScene();

        EXPECT_EQ(Pixel(6), (std::array<uint8_t, 4> {255, 158, 38, 255})); // Border of the new right half.
        EXPECT_EQ(Pixel(12), (std::array<uint8_t, 4> {31, 243, 5, 255}));  // Green material plus orange fill.
        EXPECT_EQ(Pixel(2), (std::array<uint8_t, 4> {5, 5, 5, 255}));      // No stale left silhouette.
        EXPECT_EQ(Renderer->GetStats().DrawCalls, 1u);
        if (TestBackend() == GpuTestBackend::OpenGL)
            EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));

        // Highlights are consumed by EndScene, rather than leaking into later frames.
        Renderer->BeginScene(Info);
        Renderer->EndScene();
        EXPECT_EQ(Pixel(6), (std::array<uint8_t, 4> {5, 5, 5, 255}));
        EXPECT_EQ(Pixel(12), (std::array<uint8_t, 4> {5, 5, 5, 255}));
    }

    TEST_P(MaterialSceneGL, TargetAlphaIsOpaqueEvenWhenProjectShadersReturnFractionalAlpha)
    {
        // A project shader can bypass KansOutputAlpha; opaque/mask draws must not punch holes in ImGui.
        Custom.Code       = CompileTestMaterialShader(TestMaterialVertex, R"(#version 450
layout(set = 1, binding = 0, std140) uniform Parameters { vec4 Tint; } p;
layout(location = 0) out vec4 o_Color;
void main() { o_Color = p.Tint; }
)");
        ShaderID          = Add(AssetType::Shader, EncodeShaderProduct(Custom.Code));
        const auto blend  = Quad(Color({1, 0, 0, 0.25}, MaterialAlphaMode::Blend));
        const auto opaque = Quad(Color({0, 1, 0, 0.3}));
        const auto mask   = Quad(Color({0, 0, 1, 0.3}, MaterialAlphaMode::Mask));
        Ready();
        std::unique_ptr<NativeUIFrame> ui;
        if (TestBackend() == GpuTestBackend::OpenGL)
        {
            ui = std::make_unique<NativeUIFrame>();
            glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
        }
        for (const auto& model : {blend, opaque, mask})
        {
            Renderer->BeginScene(Info);
            Renderer->SubmitModel(model, glm::mat4(1));
            Renderer->EndScene();
            EXPECT_EQ(Pixel()[3], 255);
            if (ui)
            {
                glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
                ui->Draw();
                GLboolean mask[4];
                glGetBooleanv(GL_COLOR_WRITEMASK, mask);
                EXPECT_EQ(mask[0], GL_FALSE);
                EXPECT_EQ(mask[1], GL_TRUE);
                EXPECT_EQ(mask[2], GL_FALSE);
                EXPECT_EQ(mask[3], GL_TRUE);
            }
            if (model.Model == blend.Model)
            {
                EXPECT_NEAR(Pixel()[0], 68, 1); // .25 red + .75 * 5/255 clear colour.
                EXPECT_NEAR(Pixel()[1], 4, 1);
            }
            else if (model.Model == opaque.Model)
                EXPECT_EQ(Pixel(), (std::array<uint8_t, 4> {0, 255, 0, 255}));
            else
                EXPECT_EQ(Pixel(), (std::array<uint8_t, 4> {0, 0, 255, 255}));
        }
        if (ui)
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        if (ui)
            EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
    }

    TEST_P(MaterialSceneGL, LegacyStencilOptionDrawsARingOutsideTheModelAndClearsPreviousReferences)
    {
        // Deferred submissions must preserve write-reference/exclude-reference intent, not the last GL state.
        SceneRendererSpecification spec;
        Renderer             = CreateRef<SceneRenderer>(Render.get(), spec);
        RegisterDefaultEditorFeatures(Renderer->GetFeatures());
        Renderer->SetAssetManager(Assets.get());
        const auto model = Quad(Color({0, 1, 0, 1}));
        Ready();
        auto entity = LogicScene->CreateEntity("Outlined");
        entity.AddComponent<ProductModelComponent>(model);
        entity.GetComponent<TransformComponent>().Scale = glm::vec3(0.56f);
        auto  cameraEntity                              = LogicScene->CreateEntity("Camera");
        auto& camera                                    = cameraEntity.AddComponent<CameraComponent>().SceneCamera;
        camera.SetOrthographic(2, -1, 1);
        camera.SetViewportSize(16, 16);
        auto resource                          = Render->GetRenderResource();
        resource.Piplinestate.EnableStencil    = true;
        resource.Piplinestate.EnableGridShader = false;
        Render->SetRenderResource(resource);
        // EGL's default framebuffer has no stencil bits and clamps stencil references to 0.
        // Verify restoration against a real external stencil target, as another render pass would.
        GLuint external = 0, externalColour = 0, externalDepth = 0;
        if (TestBackend() == GpuTestBackend::OpenGL)
        {
            glGenFramebuffers(1, &external);
            glBindFramebuffer(GL_FRAMEBUFFER, external);
            glGenRenderbuffers(1, &externalColour);
            glBindRenderbuffer(GL_RENDERBUFFER, externalColour);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 16, 16);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, externalColour);
            glGenRenderbuffers(1, &externalDepth);
            glBindRenderbuffer(GL_RENDERBUFFER, externalDepth);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, 16, 16);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, externalDepth);
            ASSERT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GLenum(GL_FRAMEBUFFER_COMPLETE));
        }
        for (const float x : {0.0f, -0.125f})
        {
            entity.GetComponent<TransformComponent>().Position.x = x;
            Render->OnRenderRuntime(Renderer, TimeStep(0));
            const int ringX = x == 0 ? 12 : 11;
            // Original edge .56 falls just inside a pixel centre; the 1.007 expansion reaches that pixel.
            EXPECT_EQ(Pixel(ringX), (std::array<uint8_t, 4> {227, 171, 133, 255}));
            EXPECT_EQ(Pixel(ringX - 1), (std::array<uint8_t, 4> {0, 255, 0, 255}));
            EXPECT_EQ(Pixel(ringX + 1), (std::array<uint8_t, 4> {5, 5, 5, 255}));
            EXPECT_EQ(Pixel(), (std::array<uint8_t, 4> {0, 255, 0, 255}));
            EXPECT_EQ(Renderer->GetStats().DrawCalls, 2u);
        }
        if (TestBackend() == GpuTestBackend::OpenGL)
        {
            NativeUIFrame ui;
            glBindFramebuffer(GL_FRAMEBUFFER, external);
            glEnable(GL_STENCIL_TEST);
            glStencilFuncSeparate(GL_FRONT, GL_EQUAL, 3, 0x37);
            glStencilFuncSeparate(GL_BACK, GL_GREATER, 4, 0x73);
            glStencilMaskSeparate(GL_FRONT, 0x12);
            glStencilMaskSeparate(GL_BACK, 0x21);
            glStencilOpSeparate(GL_FRONT, GL_INCR, GL_DECR, GL_INVERT);
            glStencilOpSeparate(GL_BACK, GL_ZERO, GL_REPLACE, GL_KEEP);
            glClearStencil(9);
            ui.Draw();
            EXPECT_EQ(glIsEnabled(GL_STENCIL_TEST), GL_TRUE);
            for (auto [property, expected] : {std::pair {GL_STENCIL_FUNC, GL_EQUAL},
                                              {GL_STENCIL_REF, 3},
                                              {GL_STENCIL_VALUE_MASK, 0x37},
                                              {GL_STENCIL_WRITEMASK, 0x12},
                                              {GL_STENCIL_FAIL, GL_INCR},
                                              {GL_STENCIL_PASS_DEPTH_FAIL, GL_DECR},
                                              {GL_STENCIL_PASS_DEPTH_PASS, GL_INVERT},
                                              {GL_STENCIL_BACK_FUNC, GL_GREATER},
                                              {GL_STENCIL_BACK_REF, 4},
                                              {GL_STENCIL_BACK_VALUE_MASK, 0x73},
                                              {GL_STENCIL_BACK_WRITEMASK, 0x21},
                                              {GL_STENCIL_BACK_FAIL, GL_ZERO},
                                              {GL_STENCIL_BACK_PASS_DEPTH_FAIL, GL_REPLACE},
                                              {GL_STENCIL_BACK_PASS_DEPTH_PASS, GL_KEEP},
                                              {GL_STENCIL_CLEAR_VALUE, 9}})
            {
                GLint value;
                glGetIntegerv(property, &value);
                EXPECT_EQ(value, expected) << property;
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &external);
            glDeleteRenderbuffers(1, &externalColour);
            glDeleteRenderbuffers(1, &externalDepth);
            EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
        }
    }

    TEST_P(MaterialSceneGL, LegacyOutlineCullsFrontFacesAndKeepsTheInflatedBackShell)
    {
        // Disabling culling paints the inflated front surface black outside its material silhouette.
        SceneRendererSpecification spec;
        Renderer             = CreateRef<SceneRenderer>(Render.get(), spec);
        RegisterDefaultEditorFeatures(Renderer->GetFeatures());
        Renderer->SetAssetManager(Assets.get());
        const auto material = Color({0, 1, 0, 1}, MaterialAlphaMode::Opaque, true);
        const auto front    = Quad(material, 0, false, -1, -0.439f);
        const auto back     = Quad(material, 0, true, 0, 0.561f);
        Ready();
        auto frontEntity = LogicScene->CreateEntity("Front surface");
        frontEntity.AddComponent<ProductModelComponent>(front);
        auto backEntity = LogicScene->CreateEntity("Back surface");
        backEntity.AddComponent<ProductModelComponent>(back);
        auto  cameraEntity = LogicScene->CreateEntity("Camera");
        auto& camera       = cameraEntity.AddComponent<CameraComponent>().SceneCamera;
        camera.SetOrthographic(2, -1, 1);
        camera.SetViewportSize(16, 16);
        auto resource                          = Render->GetRenderResource();
        resource.Piplinestate.EnableOutline    = true;
        resource.Piplinestate.EnableGridShader = false;
        Render->SetRenderResource(resource);
        Render->OnRenderRuntime(Renderer, TimeStep(0));
        // Pixel centres x=-.4375/.5625 sit .0015 past the original right edges.
        // The real outline shader offsets white-encoded normals by about .004 * (2/3) = .002667.
        EXPECT_EQ(Pixel(4), (std::array<uint8_t, 4> {5, 5, 5, 255}));  // Front shell must be culled.
        EXPECT_EQ(Pixel(12), (std::array<uint8_t, 4> {0, 0, 0, 255})); // Back shell must reach the edge pixel.
        EXPECT_EQ(Pixel(3), (std::array<uint8_t, 4> {0, 255, 0, 255}));
        EXPECT_EQ(Pixel(11), (std::array<uint8_t, 4> {0, 255, 0, 255}));
        EXPECT_EQ(Renderer->GetStats().DrawCalls, 4u);
        if (TestBackend() == GpuTestBackend::OpenGL)
            EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
    }

    TEST_P(MaterialSceneGL, DebugNormalLinesKeepIndexedEndpointsWithoutDepthOrAlphaWrites)
    {
        // Catches a missing endpoint expansion, wrong line stride/count, and depth
        // writes. A .05 clip displacement spans 1.6 pixels in this 64-pixel target.
        Renderer->SetViewportSize(64, 64);
        MeshData mesh;
        for (glm::vec3 p :
             {glm::vec3(-.390625f, .015625f, 0), glm::vec3(.015625f, .015625f, 0), glm::vec3(-.390625f, .421875f, 0)})
        {
            MeshVertexData v;
            v.Position = {p.x, p.y, p.z};
            v.Normal   = {1, 0, 0};
            mesh.Vertices.push_back(v);
        }
        mesh.Indices = {0, 1, 2};
        auto meshID  = Add(AssetType::StaticMesh, EncodeMeshProduct(mesh));
        Meshes.push_back(meshID);
        ModelData model;
        model.Nodes.push_back({"Root", -1, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, {meshID}});
        ProductModelComponent component;
        component.Model = Add(AssetType::Model, EncodeModelProduct(model));
        Models.push_back(component.Model);
        Ready();
        Renderer->BeginScene(Info);
        // Nonopaque destination alpha distinguishes an RGB-only normal pass
        // from one that accidentally enables alpha writes for its white output.
        auto clearedTarget = Renderer->GetRenderTarget();
        for (auto& color : clearedTarget.Colors)
        {
            color.Load          = RenderHardware::LoadOp::Clear;
            color.ClearColor[3] = .25f;
        }
        auto clear = Renderer->GetDevice()->CreateCommandList();
        clear->BeginRendering(clearedTarget);
        clear->EndRendering();
        Renderer->GetDevice()->Submit(clear);
        Renderer->SubmitModelGeometry(component, glm::mat4(1), "DebugNormalShader");
        Renderer->EndScene();
        const auto target = Renderer->GetRenderTarget();
        auto       device = Renderer->GetDevice();
        auto       read   = device->CreateBuffer(
            {64 * 64 * 8, RenderHardware::BufferUsage::CopyDestination, RenderHardware::MemoryUsage::Readback});
        auto commands = device->CreateCommandList();
        commands->CopyTextureToBuffer(target.Colors[0].Target, {}, read, 0);
        commands->CopyTextureToBuffer(target.Depth.Target, {}, read, 64 * 64 * 4);
        device->Submit(commands);
        device->WaitIdle();
        auto bytes = read->Read(0, 64 * 64 * 8);
        for (auto xy : {glm::uvec2(19, 32),
                        glm::uvec2(20, 32),
                        glm::uvec2(32, 32),
                        glm::uvec2(33, 32),
                        glm::uvec2(19, 45),
                        glm::uvec2(20, 45)})
        {
            size_t offset = (DeviceRow(xy.y, 64, device->GetCoordinateConventions()) * 64 + xy.x) * 4;
            EXPECT_EQ((std::array<uint8_t, 4> {bytes[offset], bytes[offset + 1], bytes[offset + 2], bytes[offset + 3]}),
                      (std::array<uint8_t, 4> {255, 255, 255, 64}))
                << xy.x << "," << xy.y;
        }
        for (uint32_t i = 0; i < 64 * 64; ++i)
        {
            EXPECT_EQ(bytes[i * 4 + 3], 64);
            uint32_t depth;
            std::memcpy(&depth, bytes.data() + 64 * 64 * 4 + i * 4, 4);
            ASSERT_EQ(depth >> 8, 0xffffffu);
        }
        EXPECT_EQ(Renderer->GetStats().DrawCalls, 1u);
        EXPECT_EQ(Renderer->GetStats().Triangles, 1u);
    }

    // Instrument actual driver calls, forwarding every call and restoring GLAD pointers on scope exit.
    class FrameUploadTrace
    {
    public:
        FrameUploadTrace()
        {
            Active               = this;
            glad_glBufferSubData = Record;
        }
        ~FrameUploadTrace()
        {
            glad_glBufferSubData = Original;
            Active               = nullptr;
        }
        std::vector<float> Views;
        uint32_t           Objects = 0;

    private:
        static FrameUploadTrace* Active;
        PFNGLBUFFERSUBDATAPROC   Original = glad_glBufferSubData;
        static void APIENTRY     Record(GLenum target, GLintptr offset, GLsizeiptr bytes, const void* data)
        {
            if (target == GL_COPY_WRITE_BUFFER)
            {
                if (bytes == sizeof(EngineFrameUniforms))
                    Active->Views.push_back(static_cast<const EngineFrameUniforms*>(data)->DebugView);
                else if (bytes == sizeof(EngineObjectUniforms))
                    ++Active->Objects;
            }
            Active->Original(target, offset, bytes, data);
        }
    };
    FrameUploadTrace* FrameUploadTrace::Active = nullptr;

    class RasterQueryTrace
    {
    public:
        RasterQueryTrace()
        {
            Active             = this;
            glad_glGetIntegerv = RecordInteger;
            glad_glGetBooleanv = RecordBoolean;
            glad_glIsEnabled   = RecordEnabled;
        }
        ~RasterQueryTrace()
        {
            glad_glGetIntegerv = Integer;
            glad_glGetBooleanv = Boolean;
            glad_glIsEnabled   = Enabled;
            Active             = nullptr;
        }
        uint32_t Queries = 0;

    private:
        static RasterQueryTrace* Active;
        PFNGLGETINTEGERVPROC     Integer = glad_glGetIntegerv;
        PFNGLGETBOOLEANVPROC     Boolean = glad_glGetBooleanv;
        PFNGLISENABLEDPROC       Enabled = glad_glIsEnabled;
        static void APIENTRY     RecordInteger(GLenum name, GLint* value)
        {
            ++Active->Queries;
            Active->Integer(name, value);
        }
        static void APIENTRY RecordBoolean(GLenum name, GLboolean* value)
        {
            ++Active->Queries;
            Active->Boolean(name, value);
        }
        static GLboolean APIENTRY RecordEnabled(GLenum name)
        {
            ++Active->Queries;
            return Active->Enabled(name);
        }
    };
    RasterQueryTrace* RasterQueryTrace::Active = nullptr;

    TEST_F(MaterialSceneNativeGL, DeferredSubmissionDoesNotQueryDriverRasterState)
    {
        // Per-submission GL snapshots make driver-query cost grow with every entity/outline.
        const auto model = Quad(Color({1, 0, 0, 1}));
        Ready();
        Renderer->BeginScene(Info);
        {
            RasterQueryTrace trace;
            Renderer->SubmitModel(model, glm::mat4(1), ModelStencilMode::WriteReference);
            Renderer->SubmitModel(model, glm::mat4(1));
            Renderer->SubmitModelGeometry(model, glm::mat4(1), "StencilShader", ModelGeometryMode::StencilOutline);
            Renderer->SubmitModelGeometry(model, glm::mat4(1), "OutLineShader", ModelGeometryMode::BackFaces);
            EXPECT_EQ(trace.Queries, 0u);
        }
        Renderer->EndScene();
        // Engine-owned built-ins are available without Renderer::Init; both
        // queued geometry passes now issue draws alongside the two material draws.
        EXPECT_EQ(Renderer->GetStats().DrawCalls, 4u);
    }

    TEST_F(MaterialSceneNativeGL, FrameUniformsUploadOnlyAtDebugViewTransitionsIncludingLitPreview)
    {
        // Re-uploading frame data for every object regresses scene submission cost; preview still needs Lit data.
        const auto opaque = Quad(Color({1, 0, 0, 1}));
        const auto blend  = Quad(Color({1, 0, 0, 0.25}, MaterialAlphaMode::Blend));
        Ready();
        Renderer->SetViewMode(SceneViewMode::Normals);
        FrameUploadTrace trace;
        Renderer->BeginScene(Info);
        for (int i = 0; i < 4; ++i)
            Renderer->SubmitModel(opaque, glm::mat4(1));
        Renderer->EndScene();
        EXPECT_EQ(trace.Views, (std::vector<float> {2}));
        EXPECT_EQ(trace.Objects, 4u);
        trace.Views.clear();
        trace.Objects = 0;
        Renderer->BeginScene(Info);
        Renderer->SubmitModel(opaque, glm::mat4(1));
        Renderer->SubmitModel(opaque, glm::mat4(1));
        Renderer->SetPreview(opaque, glm::mat4(1));
        Renderer->SubmitModel(blend, glm::mat4(1));
        Renderer->SubmitModel(blend, glm::mat4(1));
        Renderer->EndScene();
        EXPECT_EQ(trace.Views, (std::vector<float> {2, 0, 2}));
        EXPECT_EQ(trace.Objects, 5u);
        EXPECT_EQ(Renderer->GetStats().DrawCalls, 5u);
        if (TestBackend() == GpuTestBackend::OpenGL)
            EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
    }

    TEST_P(MaterialSceneGL, BlendSortsAcrossModelsAndPreviewByTransformedBoundsAfterOpaque)
    {
        // Submission order and object-origin distance both put red before blue, which gives the wrong colour.
        const auto near       = Quad(Color({1, 0, 0, 0.5}, MaterialAlphaMode::Blend), -0.8f);
        const auto far        = Quad(Color({0, 0, 1, 0.5}, MaterialAlphaMode::Blend), 0.8f);
        const auto background = Quad(Color({0, 1, 0, 1}), 0.9f);
        Ready();
        Renderer->BeginScene(Info);
        Renderer->SubmitModel(near, glm::translate(glm::mat4(1), {0, 0, 0.3f}));
        Renderer->SubmitModel(background, glm::mat4(1));
        Renderer->SubmitModel(far, glm::translate(glm::mat4(1), {0, 0, -0.3f}));
        Renderer->EndScene();
        const auto pixel = Pixel();
        // Blue over green, then red: (0.5, 0.25, 0.25). RGBA8 rounds each blend operation.
        EXPECT_NEAR(pixel[0], 128, 1);
        EXPECT_NEAR(pixel[1], 64, 1);
        EXPECT_NEAR(pixel[2], 64, 1);
        EXPECT_EQ(pixel[3], 255); // glBlendFunc alone reduces destination alpha and makes ImGui see through it.
        EXPECT_NEAR(Depth(), 0.95f, 1e-6f); // Transparent parts must leave the opaque depth untouched.
        EXPECT_EQ(Renderer->GetStats().DrawCalls, 3u);
        EXPECT_EQ(Renderer->GetStats().Triangles, 6u);
        Renderer->BeginScene(Info);
        Renderer->SubmitModel(near, glm::translate(glm::mat4(1), {0, 0, 0.3f}));
        Renderer->SubmitModel(background, glm::mat4(1));
        Renderer->SetPreview(far, glm::translate(glm::mat4(1), {0, 0, -0.3f}));
        Renderer->EndScene();
        // The same sorted colour, then the preview's 12% blue fill:
        // .88 * (.5,.25,.25) + .12 * (.35,.7,1) = (.482,.304,.34).
        const auto preview = Pixel();
        EXPECT_NEAR(preview[0], 123, 1);
        EXPECT_NEAR(preview[1], 78, 1);
        EXPECT_NEAR(preview[2], 87, 1);
        EXPECT_EQ(preview[3], 255);
        EXPECT_NEAR(Depth(), 0.95f, 1e-6f);
        EXPECT_EQ(Renderer->GetStats().DrawCalls, 3u);
        if (TestBackend() == GpuTestBackend::OpenGL)
            EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
    }

    TEST_P(MaterialSceneGL, BlendTestsOpaqueDepthAndLeavesUncoveredDepthClear)
    {
        // A blend behind the opaque half must be occluded, and must not write depth in the other half.
        const auto opaque = Quad(Color({0, 1, 0, 1}), 0, false, 0, 1);
        const auto blend  = Quad(Color({1, 0, 0, 0.5}, MaterialAlphaMode::Blend), 0.5f);
        Ready();
        if (TestBackend() == GpuTestBackend::OpenGL)
            glDisable(GL_DEPTH_TEST);
        Renderer->BeginScene(Info);
        Renderer->SubmitModel(blend, glm::mat4(1));
        Renderer->SubmitModel(opaque, glm::mat4(1));
        Renderer->EndScene();
        EXPECT_EQ(Pixel(12), (std::array<uint8_t, 4> {0, 255, 0, 255}));
        EXPECT_NEAR(Pixel(4)[0], 130, 1); // 0.5 red + 0.5 * the 5/255 clear colour.
        EXPECT_EQ(Pixel(4)[3], 255);
        EXPECT_FLOAT_EQ(Depth(4), 1.0f);
        EXPECT_NEAR(Depth(12), 0.5f, 1e-6f);
    }

    TEST_P(MaterialSceneGL, BlendDistanceUsesTheCameraAndEachPartsBoundsWithinOneModel)
    {
        // Equal-depth parts need radial distance from the camera, not node origin or camera-space Z.
        Quad(Color({1, 0, 0, 0.5}, MaterialAlphaMode::Blend), 0, false, -0.2f, 1);
        Quad(Color({0, 0, 1, 0.5}, MaterialAlphaMode::Blend), 0, false, -1, 0.2f);
        ModelData model;
        model.Nodes.push_back({"Root", -1, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, Meshes});
        ProductModelComponent component;
        component.Model = Add(AssetType::Model, EncodeModelProduct(model));
        Models.push_back(component.Model);
        Ready();
        Info.sceneCamera.Position = {4, 0, 0}; // Red centre x=0.4 is nearer than blue centre x=-0.4.
        Renderer->BeginScene(Info);
        Renderer->SubmitModel(component, glm::mat4(1));
        Renderer->EndScene();
        const auto pixel = Pixel();
        EXPECT_NEAR(pixel[0], 129, 1); // red .5 + blue .25 + clear .25
        EXPECT_NEAR(pixel[2], 65, 1);
        EXPECT_EQ(pixel[3], 255);
    }

    TEST_P(MaterialSceneGL, StandardMaskMultipliesTextureFactorAndVertexAlphaAndRetainsCutoffEquality)
    {
        TextureData texture;
        texture.Width        = 3;
        texture.Height       = 1;
        texture.Usage        = TextureUsage::Data;
        texture.Mips         = {{{std::byte {255},
                                  std::byte {255},
                                  std::byte {255},
                                  std::byte {32},
                                  std::byte {255},
                                  std::byte {255},
                                  std::byte {255},
                                  std::byte {224},
                                  std::byte {255},
                                  std::byte {255},
                                  std::byte {255},
                                  std::byte {255}}}};
        const auto textureID = Add(AssetType::Texture, EncodeTextureProduct(texture));
        auto&      cache     = *Renderer->GetAssetCache();
        ASSERT_TRUE(Frames(cache, [&] { return cache.GetTexture(textureID) != nullptr; }));
        RenderHardware::SamplerDesc nearest;
        nearest.Min = nearest.Mag = nearest.Mip = RenderHardware::Filter::Nearest;
        cache.SetTextureSampler(textureID, nearest);
        MaterialSource source;
        source.Parameters               = {{"BaseColor", {1, 0, 0, 0.75}}, {"VertexColorStrength", {1}},
                                           {"EmissiveColor", {1, 0, 0}}};
        source.Textures["BaseColorMap"] = textureID;
        source.Render.AlphaMode         = MaterialAlphaMode::Mask;
        source.Render.AlphaCutoff       = 0.35f;
        const auto mask                 = Quad(Material(source, false), 0, false, -1, 1, 0.5f);
        source.Render.AlphaCutoff       = 0.375f;
        const auto equality             = Quad(Material(source, false), 0, false, -1, 1, 0.5f);
        source.Render.AlphaCutoff       = 0.25f;
        const auto lowCutoff            = Quad(Material(source, false), 0, false, -1, 1, 0.5f);
        source.Render.AlphaMode         = MaterialAlphaMode::Opaque;
        const auto opaque               = Quad(Material(source, false), 0, false, -1, 1, 0.5f);
        source.Render.AlphaMode         = MaterialAlphaMode::Blend;
        const auto blend                = Quad(Material(source, false), 0, false, -1, 1, 0.5f);
        Ready();
        Renderer->SetViewMode(SceneViewMode::BaseColor);
        for (auto model : {mask, equality, lowCutoff, opaque})
        {
            Renderer->BeginScene(Info);
            Renderer->SubmitModel(model, glm::mat4(1));
            Renderer->EndScene();
            const bool isOpaque = model.Model == opaque.Model;
            EXPECT_EQ(Pixel(2),
                      (isOpaque ? std::array<uint8_t, 4> {255, 0, 0, 255} : std::array<uint8_t, 4> {5, 5, 5, 255}));
            EXPECT_EQ(Pixel(8),
                      ((isOpaque || model.Model == lowCutoff.Model) ? std::array<uint8_t, 4> {255, 0, 0, 255} :
                                                                      std::array<uint8_t, 4> {5, 5, 5, 255}));
            EXPECT_EQ(Pixel(13),
                      (std::array<uint8_t, 4> {255, 0, 0, 255})); // alpha == cutoff survives; output alpha is 1.
            EXPECT_NEAR(Depth(2), isOpaque ? 0.5f : 1.0f, 1e-6f); // Discard must also leave a depth hole.
        }
        Renderer->BeginScene(Info);
        Renderer->SubmitModel(blend, glm::mat4(1));
        Renderer->EndScene();
        // Blend must use that same final alpha: .75 * .5 * 1 = .375 on the right texel.
        EXPECT_NEAR(Pixel(13)[0], 99, 1); // .375 * 255 + .625 * 5.
        EXPECT_NEAR(Pixel(8)[0], 87, 1);  // .375 * (224/255) * 255 + remaining clear colour.
        EXPECT_EQ(Pixel(13)[3], 255);
        EXPECT_FLOAT_EQ(Depth(13), 1.0f);
        Renderer->SetViewMode(SceneViewMode::Lit);
        Info.pointLight.Ambient_Intensity = {0, 0, 0};
        Renderer->BeginScene(Info);
        Renderer->SubmitModel(blend, glm::mat4(1));
        Renderer->EndScene();
        // Lit output must retain alpha after KansToneMap: emission=1 gives pow(1/2,1/2.2).
        EXPECT_NEAR(Pixel(13)[0], 73, 1); // .375 * .72974 * 255 + .625 * 5; forgetting output alpha gives 186.
        EXPECT_EQ(Pixel(13)[3], 255);
    }

    TEST_P(MaterialSceneGL, FailedCustomShaderAndDebugFallbackKeepBlendAndDoubleSidedState)
    {
        // Falling back to Standard must not reset render state along with the incompatible named parameters.
        const auto     broken   = CompileTestMaterialShader(TestMaterialVertex, R"(#version 450
layout(location = 1) out vec4 o_Color;
void main() { o_Color = vec4(1); }
)");
        const auto     brokenID = Add(AssetType::Shader, EncodeShaderProduct(broken));
        MaterialSource source;
        source.Shader = ShaderID;
        source.Render = {MaterialAlphaMode::Blend, 0.375f, true};
        auto resolved = ResolveMaterialSource(source, &Custom);
        ASSERT_TRUE(std::holds_alternative<MaterialData>(resolved));
        auto data           = std::get<MaterialData>(resolved);
        data.Shader         = brokenID;
        const auto material = Add(AssetType::Material, EncodeMaterialProduct(data));
        Materials.push_back(material);
        const auto quad = Quad(material, 0, true);
        Ready();
        for (auto mode : {SceneViewMode::Lit, SceneViewMode::BaseColor})
        {
            Renderer->SetViewMode(mode);
            Renderer->BeginScene(Info);
            Renderer->SubmitModel(quad, glm::mat4(1));
            Renderer->EndScene();
            EXPECT_GT(Pixel()[0], 150);     // Still double-sided when the Shader changes.
            EXPECT_FLOAT_EQ(Depth(), 1.0f); // Still blend: no depth writes even though Standard's fallback alpha is 1.
            EXPECT_EQ(Pixel()[3], 255);
        }
    }

    TEST_P(MaterialSceneGL, DoubleSidedBackFacesFlipNormalsAndDoNotDisableCullingForTheNextPart)
    {
        MaterialSource source;
        source.Parameters["BaseColor"] = {1, 0, 0, 1};
        source.Render.DoubleSided      = true;
        const auto doubleSided         = Quad(Material(source, false), 0, true, -1, 0);
        source.Render.DoubleSided      = false;
        const auto singleSided         = Quad(Material(source, false), 0, true, 0, 1);
        Ready();
        if (TestBackend() == GpuTestBackend::OpenGL)
            glDisable(GL_CULL_FACE);
        Renderer->SetViewMode(SceneViewMode::Normals);
        Renderer->BeginScene(Info);
        Renderer->SubmitModel(doubleSided, glm::mat4(1));
        Renderer->SubmitModel(singleSided, glm::mat4(1));
        Renderer->EndScene();
        EXPECT_EQ(Pixel(4)[2], 0); // +Z geometric normal becomes -Z on the visible back face.
        EXPECT_NEAR(Pixel(4)[0], 128, 1);
        EXPECT_EQ(Pixel(12), (std::array<uint8_t, 4> {5, 5, 5, 255}));
        Renderer->SetViewMode(SceneViewMode::Lit);
        Renderer->BeginScene(Info);
        Renderer->SubmitModel(doubleSided, glm::mat4(1));
        Renderer->EndScene();
        EXPECT_GT(Pixel(4)[0], 150); // A light on -Z must illuminate the flipped back-face normal.
        if (TestBackend() == GpuTestBackend::OpenGL)
            EXPECT_EQ(glIsEnabled(GL_CULL_FACE), GL_FALSE);
    }

    TEST_F(MaterialSceneNativeGL, ImGuiRestoresDepthCullBlendFunctionsAndWireframeModeAfterPreviewOutline)
    {
        // Bug: the surviving native UI renderer leaks raster state after rendering over a scene preview.
        const auto model = Quad(Color({1, 0, 0, 0.5}, MaterialAlphaMode::Blend, true), 0, false, -0.5f, 0.5f);
        Ready();
        Renderer->BeginScene(Info);
        Renderer->SetPreview(model, glm::mat4(1));
        Renderer->EndScene();
        NativeUIFrame ui;
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
        if (TestBackend() == GpuTestBackend::OpenGL)
            glDisable(GL_DEPTH_TEST);
        glDepthFunc(GL_GREATER);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);
        glBlendEquationSeparate(GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        ui.Draw();
        if (TestBackend() == GpuTestBackend::OpenGL)
            EXPECT_EQ(glIsEnabled(GL_CULL_FACE), GL_TRUE);
        if (TestBackend() == GpuTestBackend::OpenGL)
            EXPECT_EQ(glIsEnabled(GL_DEPTH_TEST), GL_FALSE);
        if (TestBackend() == GpuTestBackend::OpenGL)
            EXPECT_EQ(glIsEnabled(GL_BLEND), GL_TRUE);
        GLboolean depthWrite;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
        EXPECT_EQ(depthWrite, GL_FALSE);
        for (auto [property, expected] : {std::pair {GL_CULL_FACE_MODE, GL_FRONT},
                                          {GL_DEPTH_FUNC, GL_GREATER},
                                          {GL_BLEND_SRC_RGB, GL_ONE},
                                          {GL_BLEND_DST_RGB, GL_ZERO},
                                          {GL_BLEND_SRC_ALPHA, GL_ONE},
                                          {GL_BLEND_DST_ALPHA, GL_ZERO},
                                          {GL_BLEND_EQUATION_RGB, GL_FUNC_SUBTRACT},
                                          {GL_BLEND_EQUATION_ALPHA, GL_FUNC_REVERSE_SUBTRACT}})
        {
            GLint actual;
            glGetIntegerv(property, &actual);
            EXPECT_EQ(actual, expected) << property;
        }
        GLint polygon[2];
        glGetIntegerv(GL_POLYGON_MODE, polygon);
        EXPECT_EQ(polygon[0], GLint(GL_LINE));
        EXPECT_EQ(Pixel(8)[3], 255);
        if (TestBackend() == GpuTestBackend::OpenGL)
            EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
    }

    class SurfacePickerGL : public RenderAssetCacheGL,
                            public testing::WithParamInterface<std::tuple<GpuTestBackend, bool>>
    {
    protected:
        GpuTestBackend TestBackend() const override { return std::get<0>(GetParam()); }
    };

    TEST_P(SurfacePickerGL, UnprojectsNearestDepthAtPixelCentreAndClearsPreviousHit)
    {
        MeshData quad;
        for (auto [x, y] : {std::pair {-3.0f, -3.0f}, {3.0f, -3.0f}, {3.0f, 3.0f}, {-3.0f, 3.0f}})
        {
            MeshVertexData vertex;
            vertex.Position = {x, y, 0};
            quad.Vertices.push_back(vertex);
        }
        quad.Indices  = {0, 1, 2, 2, 3, 0};
        auto uploaded = UploadMesh(quad);
        ASSERT_TRUE(std::holds_alternative<GpuMesh>(uploaded));
        const auto&     mesh           = std::get<GpuMesh>(uploaded);
        const bool      perspective    = std::get<1>(GetParam());
        const glm::mat4 projection     = perspective ? glm::perspective(glm::radians(90.0f), 2.0f, 1.0f, 9.0f) :
                                                       glm::ortho(-8.0f, 8.0f, -4.0f, 4.0f, 1.0f, 9.0f);
        const glm::mat4 viewProjection = projection * glm::translate(glm::mat4(1.0f), {-10, -20, -30});
        const MeshDraw  near {&mesh, glm::translate(glm::mat4(1.0f), {10, 20, 26}), 7};
        const MeshDraw  far {&mesh, glm::translate(glm::mat4(1.0f), {10, 20, 24}), 9};

        // Picking must establish its depth state even after other render passes.
        if (TestBackend() == GpuTestBackend::OpenGL)
        {
            glDepthMask(GL_FALSE);
            glDepthFunc(GL_GREATER);
            glDepthRange(0.25, 0.75);
            glClearDepth(0.0);
        }
        EntityPicker picker;
        const auto   hit = picker.PickSurface({near, far}, viewProjection, {32, 16}, {19, 5});
        ASSERT_EQ(hit.ID, 7u);
        ASSERT_TRUE(hit.Position);
        // NDC=(7/32,-5/16); at z=-4 both views span 16x8. Catches +0.5,
        // flipped Y, viewport aspect, depth range mapping, and missing inverse view.
        // 24-bit depth at these near/far planes gives <2e-6 world error; allow 2e-5 for matrix arithmetic.
        EXPECT_NEAR(hit.Position->x, 11.75f, 2e-5f);
        EXPECT_NEAR(hit.Position->y, 18.75f, 2e-5f);
        EXPECT_NEAR(hit.Position->z, 26.0f, 2e-5f);
        if (TestBackend() == GpuTestBackend::OpenGL)
        {
            NativeUIFrame ui;
            glDepthMask(GL_FALSE);
            glDepthFunc(GL_GREATER);
            ui.Draw();
            GLboolean depthMask     = GL_TRUE;
            GLint     depthFunction = 0;
            glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
            glGetIntegerv(GL_DEPTH_FUNC, &depthFunction);
            EXPECT_EQ(depthMask, GL_FALSE);
            EXPECT_EQ(depthFunction, GL_GREATER);
        }
        // Reusing the pixel must clear its old nearer depth.
        const auto farther = picker.PickSurface({far}, viewProjection, {32, 16}, {19, 5});
        ASSERT_EQ(farther.ID, 9u);
        ASSERT_TRUE(farther.Position);
        EXPECT_NEAR(farther.Position->x, perspective ? 12.625f : 11.75f, 2e-5f);
        EXPECT_NEAR(farther.Position->y, perspective ? 18.125f : 18.75f, 2e-5f);
        EXPECT_NEAR(farther.Position->z, 24.0f, 2e-5f);
        // An uncovered pixel must lose both the previous ID and its world point.
        const MeshDraw elsewhere {&mesh, glm::translate(glm::mat4(1.0f), {100, 20, 26}), 7};
        const auto empty = picker.PickSurface({elsewhere}, viewProjection, {32, 16}, {19, 5});
        EXPECT_EQ(empty.ID, 0u);
        EXPECT_FALSE(empty.Position);
        if (TestBackend() == GpuTestBackend::OpenGL)
            EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
    }

    INSTANTIATE_TEST_SUITE_P(Projections,
                             SurfacePickerGL,
                             testing::Combine(testing::Values(GpuTestBackend::OpenGL, GpuTestBackend::Vulkan),
                                              testing::Values(true, false)),
                             [](const testing::TestParamInfo<std::tuple<GpuTestBackend, bool>>& info) {
                                 return std::string(std::get<0>(info.param) == GpuTestBackend::OpenGL ? "OpenGL" :
                                                                                                        "Vulkan") +
                                        (std::get<1>(info.param) ? "Perspective" : "Orthographic");
                             });

    // Picking reads the ID under a pixel; the outline borders a silhouette.
    TEST_P(RenderAssetCacheGPU, PickerFindsMeshesAndOutlineBordersThem)
    {
        constexpr int Size = 16;
        MeshData      quad;
        for (auto [x, y] : {std::pair {-1.0f, -1.0f}, {0.0f, -1.0f}, {0.0f, 1.0f}, {-1.0f, 1.0f}})
        {
            MeshVertexData vertex;
            vertex.Position = {x, y, 0};
            quad.Vertices.push_back(vertex);
        }
        quad.Indices  = {0, 1, 2, 2, 3, 0};
        auto uploaded = UploadMesh(quad);
        ASSERT_TRUE(std::holds_alternative<GpuMesh>(uploaded));
        const auto& mesh  = std::get<GpuMesh>(uploaded);
        EXPECT_EQ(mesh.LocalBounds.Min, glm::vec3(-1, -1, 0));
        EXPECT_EQ(mesh.LocalBounds.Max, glm::vec3(0, 1, 0));
        const std::vector<MeshDraw> draws = {{&mesh, glm::mat4(1.0f), 7}};

        EntityPicker picker;
        EXPECT_EQ(picker.Pick(draws, glm::mat4(1.0f), {Size, Size}, {4, 8}), 7u);
        EXPECT_EQ(picker.Pick(draws, glm::mat4(1.0f), {Size, Size}, {12, 8}), 0u);
        EXPECT_EQ(picker.Pick({}, glm::mat4(1.0f), {Size, Size}, {4, 8}), 0u);
        if (TestBackend() == GpuTestBackend::OpenGL)
            EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));

        GLuint framebuffer = 0, color = 0;
        if (TestBackend() == GpuTestBackend::OpenGL)
        {
            glGenFramebuffers(1, &framebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glGenTextures(1, &color);
            glBindTexture(GL_TEXTURE_2D, color);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, Size, Size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
            glViewport(0, 0, Size, Size);
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        auto                        device = mesh.Device;
        RenderHardware::TextureDesc outlineDesc;
        outlineDesc.Width = outlineDesc.Height = Size;
        outlineDesc.Usage  = RenderHardware::TextureUsage::RenderTarget | RenderHardware::TextureUsage::CopySource;
        auto outlineTarget = device->CreateTexture(outlineDesc);
        if (TestBackend() == GpuTestBackend::OpenGL)
        {
            glFramebufferTexture2D(GL_FRAMEBUFFER,
                                   GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D,
                                   GLuint(device->GetImGuiTextureID(outlineTarget)),
                                   0);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        RenderHardware::RenderingDesc target;
        target.Colors = {
            {outlineTarget, {}, RenderHardware::LoadOp::Clear, RenderHardware::StoreOp::Store, {0, 0, 0, 1}}};
        auto clear = device->CreateCommandList();
        clear->BeginRendering(target);
        clear->EndRendering();
        device->Submit(clear);
        target.Colors[0].Load = RenderHardware::LoadOp::Load;
        SelectionOutline outline(device);
        outline.Draw(target, draws, glm::mat4(1.0f), {Size, Size}, {1, 0, 0, 1}, 2.0f);
        if (TestBackend() == GpuTestBackend::OpenGL)
            EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
        std::array<uint8_t, Size * Size * 4> pixels {};
        if (TestBackend() == GpuTestBackend::OpenGL)
        {
            NativeUIFrame ui;
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            ui.Draw();
            // ImGui restores the caller's target; outline pixels are still read from that image.
            GLint bound = 0;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &bound);
            EXPECT_EQ(GLuint(bound), framebuffer);
            glReadPixels(0, 0, Size, Size, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        }
        else
        {
            auto bytes = ReadImage(outlineTarget);
            std::copy(bytes.begin(), bytes.end(), pixels.begin());
        }
        const auto red = [&](int x, int y) { return pixels[(y * Size + x) * 4]; };
        EXPECT_EQ(red(9, 8), 255);  // Border just right of the quad.
        EXPECT_EQ(red(14, 8), 0);   // Far from it.
        EXPECT_GT(red(4, 8), 0);    // Faint fill inside.
        EXPECT_LT(red(4, 8), 80);
        if (TestBackend() == GpuTestBackend::OpenGL)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteTextures(1, &color);
            glDeleteFramebuffers(1, &framebuffer);
        }
    }
    class MaterialPreviewGL : public MaterialSceneGL
    {
    protected:
        MaterialPreviewOptions Options {{64, 64}, MaterialPreviewShape::Plane, {0, 0}};
        MaterialData           Resolve(const MaterialSource& source, const ShaderData* shader = nullptr)
        {
            auto result = ResolveMaterialSource(source, shader);
            if (auto* error = std::get_if<AssetError>(&result))
                throw std::runtime_error(error->Message);
            return std::get<MaterialData>(std::move(result));
        }
        std::array<uint8_t, 4> PreviewPixel(uint32_t texture, int x = 32, int y = 32)
        {
            std::vector<uint8_t> image(size_t(Options.Size.x) * Options.Size.y * 4);
            glBindTexture(GL_TEXTURE_2D, texture);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.data());
            std::array<uint8_t, 4> pixel;
            std::copy_n(image.data() + (y * Options.Size.x + x) * 4, 4, pixel.data());
            return pixel;
        }
        std::array<uint8_t, 4>
        PreviewPixel(const RenderHardware::Handle<RenderHardware::Texture>& texture, int x = 32, int y = 32)
        {
            auto   device = Renderer->GetDevice();
            size_t bytes  = size_t(Options.Size.x) * Options.Size.y * 4;
            auto   buffer = device->CreateBuffer(
                {bytes, RenderHardware::BufferUsage::CopyDestination, RenderHardware::MemoryUsage::Readback});
            auto commands = device->CreateCommandList();
            commands->CopyTextureToBuffer(texture, {}, buffer);
            device->Submit(commands);
            device->WaitIdle();
            auto image = buffer->Read(0, bytes);
            y          = DeviceRow(y, Options.Size.y, device->GetCoordinateConventions());
            std::array<uint8_t, 4> pixel;
            std::copy_n(image.data() + (y * Options.Size.x + x) * 4, 4, pixel.data());
            return pixel;
        }
        MaterialSource Tint(std::vector<double> color)
        {
            MaterialSource source;
            source.Shader             = ShaderID;
            source.Parameters["Tint"] = std::move(color);
            return source;
        }
    };

    class MaterialPreviewNativeGL : public MaterialPreviewGL
    {
    protected:
        GpuTestBackend TestBackend() const override { return GpuTestBackend::OpenGL; }
    };

    TEST_P(MaterialPreviewGL, UnsavedValuesAndShaderChangesReachPixelsInLitMode)
    {
        MaterialPreviewRenderer preview(*Renderer);
        Renderer->SetViewMode(SceneViewMode::Normals);
        auto source = Tint({1, 0, 0, 1});
        auto first  = preview.RenderImage(Resolve(source, &Custom), &Custom, Options);
        ASSERT_TRUE(first.Complete);
        EXPECT_EQ(PreviewPixel(first.Target), (std::array<uint8_t, 4> {255, 0, 0, 255}));
        source.Parameters["Tint"] = {0, 1, 0, 1};
        auto second               = preview.RenderImage(Resolve(source, &Custom), &Custom, Options);
        EXPECT_EQ(PreviewPixel(second.Target), (std::array<uint8_t, 4> {0, 255, 0, 255}));
        // ShaderData may be reloaded at the same address; pointer identity is not a cache key.
        Custom.Code               = CompileTestMaterialShader(TestMaterialVertex, R"(#version 450
#include <Kans3D/Engine.glsl>
layout(set=1,binding=0,std140) uniform Parameters { vec4 Tint; } p;
layout(location=0) out vec4 color;
void main() { color = vec4(p.Tint.bgr + vec3(u_Frame.DebugView), 1); }
)");
        source.Parameters["Tint"] = {1, 0, 0, 1};
        auto changed              = preview.RenderImage(Resolve(source, &Custom), &Custom, Options);
        EXPECT_EQ(PreviewPixel(changed.Target), (std::array<uint8_t, 4> {0, 0, 255, 255}));
    }
    TEST_P(MaterialPreviewGL, BlendCompositesOverBothCheckerColorsAndKeepsOpaqueAlpha)
    {
        MaterialPreviewRenderer preview(*Renderer);
        auto                    source = Tint({1, 0, 0, 0.5});
        source.Render.AlphaMode        = MaterialAlphaMode::Blend;
        auto result                    = preview.RenderImage(Resolve(source, &Custom), &Custom, Options);
        ASSERT_TRUE(result.Complete);
        // 50% red over checker .22 and .32, without changing its alpha.
        auto dark = PreviewPixel(result.Target), light = PreviewPixel(result.Target, 24, 32);
        EXPECT_NEAR(dark[0], 156, 1);
        EXPECT_NEAR(dark[1], 28, 1);
        EXPECT_NEAR(dark[2], 28, 1);
        EXPECT_NEAR(light[0], 168, 1);
        EXPECT_NEAR(light[1], 41, 1);
        EXPECT_NEAR(light[2], 41, 1);
        EXPECT_EQ(dark[3], 255);
        EXPECT_EQ(light[3], 255);
    }
    TEST_P(MaterialPreviewGL, MaskCutoffChangesCoverageAndRetainsEquality)
    {
        MaterialPreviewRenderer preview(*Renderer);
        auto                    source = Tint({1, 0, 0, 0.5});
        source.Render.AlphaMode        = MaterialAlphaMode::Mask;
        source.Render.AlphaCutoff      = 0.6f;
        auto clipped                   = preview.RenderImage(Resolve(source, &Custom), &Custom, Options);
        EXPECT_EQ(PreviewPixel(clipped.Target), (std::array<uint8_t, 4> {56, 56, 56, 255}));
        source.Render.AlphaCutoff = 0.5f;
        auto equality             = preview.RenderImage(Resolve(source, &Custom), &Custom, Options);
        EXPECT_EQ(PreviewPixel(equality.Target), (std::array<uint8_t, 4> {255, 0, 0, 255}));
    }
    TEST_P(MaterialPreviewGL, YawPitchAndDoubleSidedControlPlaneVisibility)
    {
        MaterialPreviewRenderer preview(*Renderer);
        auto                    source = Tint({0, 1, 0, 1});
        Options.Rotation.x             = glm::radians(90.0f);
        auto edge                      = preview.RenderImage(Resolve(source, &Custom), &Custom, Options);
        EXPECT_EQ(PreviewPixel(edge.Target), (std::array<uint8_t, 4> {56, 56, 56, 255}));
        Options.Rotation = {0, glm::radians(90.0f)};
        edge             = preview.RenderImage(Resolve(source, &Custom), &Custom, Options);
        EXPECT_EQ(PreviewPixel(edge.Target), (std::array<uint8_t, 4> {56, 56, 56, 255}));
        Options.Rotation = {glm::radians(180.0f), 0};
        auto back        = preview.RenderImage(Resolve(source, &Custom), &Custom, Options);
        EXPECT_EQ(PreviewPixel(back.Target), (std::array<uint8_t, 4> {56, 56, 56, 255}));
        source.Render.DoubleSided = true;
        back                      = preview.RenderImage(Resolve(source, &Custom), &Custom, Options);
        EXPECT_EQ(PreviewPixel(back.Target), (std::array<uint8_t, 4> {0, 255, 0, 255}));
    }
    TEST_P(MaterialPreviewGL, ProceduralTangentsMapNormalAlongIncreasingTextureV)
    {
        // A normal map pointing along +V detects zero tangents and wrong sphere handedness.
        ShaderData shader;
        shader.Code = CompileTestMaterialShader(R"(#version 450
#include <Kans3D/Engine.glsl>
#include <Kans3D/VertexInput.glsl>
layout(location=0) out vec3 N;
layout(location=1) out vec4 T;
void main() { N=a_Normal; T=a_Tangent;
    gl_Position=u_Frame.ViewProjection*u_Object.Transform*vec4(a_Position,1); }
)",
                                                R"(#version 450
layout(location=0) in vec3 N;
layout(location=1) in vec4 T;
layout(set=1,binding=0) uniform sampler2D NormalMap;
layout(location=0) out vec4 color;
void main() {
    vec3 n=normalize(N), t=normalize(T.xyz-n*dot(n,T.xyz));
    vec3 mapped=texture(NormalMap,vec2(0.5)).xyz*2-1;
    color=vec4((mat3(t,cross(n,t)*T.w,n)*mapped)*0.5+0.5,1);
}
)");
        TextureData normal;
        normal.Width = normal.Height = 1;
        normal.Format                = TexturePixelFormat::RGBA32Float;
        normal.Usage                 = TextureUsage::Data;
        const std::array<float, 4> pixel {0.5, 1, 0.5, 1};
        normal.Mips = {{std::vector<std::byte>(sizeof(pixel))}};
        std::memcpy(normal.Mips[0].Pixels.data(), pixel.data(), sizeof(pixel));
        auto texture = Add(AssetType::Texture, EncodeTextureProduct(normal));
        ASSERT_TRUE(Frames(*Renderer->GetAssetCache(),
                           [&] { return Renderer->GetAssetCache()->GetTexture(texture) != nullptr; }));
        MaterialSource source;
        source.Shader                = ShaderID;
        source.Textures["NormalMap"] = texture;
        MaterialPreviewRenderer preview(*Renderer);
        for (auto shape : {MaterialPreviewShape::Plane, MaterialPreviewShape::Cube, MaterialPreviewShape::Sphere})
        {
            Options.Shape = shape;
            // Inspect a side face too: its tangent cannot be the vertex default +X.
            Options.Rotation.x = shape == MaterialPreviewShape::Cube ? glm::radians(90.0f) : 0.0f;
            auto result        = preview.RenderImage(Resolve(source, &shader), &shader, Options);
            ASSERT_TRUE(result.Complete);
            auto value = PreviewPixel(result.Target);
            EXPECT_NEAR(value[0], 128, 1);
            EXPECT_NEAR(value[1], shape == MaterialPreviewShape::Sphere ? 0 : 255, 1);
            // At center+half a pixel, sphere y ~= .016, hence B.z ~= .016 -> encoded 130.
            EXPECT_NEAR(value[2], shape == MaterialPreviewShape::Sphere ? 130 : 128, 1);
        }
    }
    TEST_P(MaterialPreviewGL, StandardRemainsLitAndUsesSceneIrradianceChanges)
    {
        MaterialPreviewRenderer preview(*Renderer);
        Renderer->SetViewMode(SceneViewMode::Normals);
        auto& environment = const_cast<Environment&>(Renderer->GetEnvironment());
        // Replace an RHI-owned scene map between calls; previews must resolve the new handle.
        auto map = [&](glm::vec4 color) {
            using namespace RenderHardware;
            TextureDesc desc;
            desc.Dimension   = TextureDimension::Cube;
            desc.PixelFormat = Format::RGBA32Float;
            desc.Usage       = RenderHardware::TextureUsage::Sampled | RenderHardware::TextureUsage::CopyDestination;
            auto device      = Renderer->GetDevice();
            auto cube        = device->CreateTexture(desc);
            auto buffer      = device->CreateBuffer({sizeof(color), BufferUsage::CopySource, MemoryUsage::Upload});
            buffer->Write(0, &color, sizeof(color));
            auto commands = device->CreateCommandList();
            for (uint32_t face = 0; face < 6; ++face)
                commands->CopyBufferToTexture(buffer, 0, cube, {0, face});
            device->Submit(commands);
            device->WaitIdle();
            return cube;
        };
        MaterialSource source;
        source.Parameters         = {{"BaseColor", {0.25, 0.25, 0.25, 1}}, {"Roughness", {1}}};
        auto material             = Resolve(source);
        environment.IrradianceMap = map({4, 0, 0, 1});
        auto red                  = preview.RenderImage(material, nullptr, Options);
        auto redPixel             = PreviewPixel(red.Target);
        environment.IrradianceMap = map({0, 4, 0, 1});
        auto green                = preview.RenderImage(material, nullptr, Options);
        auto greenPixel           = PreviewPixel(green.Target);
        // At the front-facing center, kD=.96 and diffuse=.25*4=.1e1:
        // Key light adds about .0453; Reinhard+gamma(.96+.0453)=.7306,
        // Reinhard+gamma(.0453)=.2399. Debug normals or missing IBL cannot match these.
        EXPECT_NEAR(redPixel[0], 186, 1);
        EXPECT_NEAR(redPixel[1], 61, 1);
        EXPECT_NEAR(redPixel[2], 61, 1);
        EXPECT_NEAR(greenPixel[1], 186, 1);
        EXPECT_NEAR(greenPixel[0], 61, 1);
        EXPECT_NEAR(greenPixel[2], 61, 1);
        EXPECT_EQ(redPixel[3], 255);
        EXPECT_EQ(greenPixel[3], 255);
    }
    TEST_P(MaterialPreviewGL, FailedCustomShaderFallsBackWithRenderStateAndIncompleteResult)
    {
        MaterialPreviewRenderer preview(*Renderer);
        ShaderData              broken;
        broken.Code               = CompileTestMaterialShader(TestMaterialVertex, R"(#version 450
layout(location=1) out vec4 wrongOutput;
void main() { wrongOutput=vec4(1); }
)");
        auto source               = Tint({1, 0, 0, 1});
        source.Render.DoubleSided = true;
        Options.Rotation.x        = glm::radians(180.0f);
        auto result               = preview.RenderImage(Resolve(source, &Custom), &broken, Options);
        EXPECT_FALSE(result.Complete);
        auto pixel = PreviewPixel(result.Target);
        // Standard fallback on the back of the plane must still draw because double-sided survived.
        EXPECT_GT(pixel[0], 56);
        EXPECT_EQ(pixel[0], pixel[1]);
        EXPECT_EQ(pixel[1], pixel[2]);
        EXPECT_EQ(pixel[3], 255);
    }
    TEST_P(MaterialPreviewGL, ThumbnailWaitsForTextureThenReadsTopRowFirstAfterReimport)
    {
        ShaderData shader;
        shader.Code          = CompileTestMaterialShader(TestMaterialVertex, R"(#version 450
layout(location=0) in vec2 v_UV;
layout(set=1,binding=0) uniform sampler2D image;
layout(location=0) out vec4 color;
void main() { color=vec4(texture(image,v_UV).r, v_UV.y, 0, 1); }
)");
        auto        shaderID = Add(AssetType::Shader, EncodeShaderProduct(shader.Code));
        TextureData texture;
        texture.Width = texture.Height = 1;
        texture.Usage                  = TextureUsage::Data;
        texture.Mips                   = {{{std::byte {255}, std::byte {0}, std::byte {0}, std::byte {255}}}};
        auto           textureID       = Add(AssetType::Texture, EncodeTextureProduct(texture));
        MaterialSource source;
        source.Shader            = shaderID;
        source.Textures["image"] = textureID;
        auto data                = Resolve(source, &shader);
        auto materialID          = Add(AssetType::Material, EncodeMaterialProduct(data));
        ASSERT_TRUE(std::holds_alternative<Ref<const MaterialDataAsset>>(Assets->Load<MaterialDataAsset>(materialID)));
        ASSERT_TRUE(std::holds_alternative<Ref<const ShaderDataAsset>>(Assets->Load<ShaderDataAsset>(shaderID)));
        Provider->SetClosed(true);
        MaterialPreviewRenderer preview(*Renderer);
        // Material/shader are loaded, but the assigned texture is blocked on the worker.
        EXPECT_FALSE(preview.RenderImage(materialID, Options).Complete);
        EXPECT_FALSE(preview.RenderThumbnail(materialID, Options).has_value());
        Provider->SetClosed(false);
        ASSERT_TRUE(Frames(*Renderer->GetAssetCache(),
                           [&] { return Renderer->GetAssetCache()->GetTexture(textureID) != nullptr; }));
        auto thumbnail = preview.RenderThumbnail(materialID, Options);
        ASSERT_TRUE(thumbnail.has_value());
        auto at = [&](const MaterialThumbnail& image, int y) { return image.Pixels[(y * 64 + 32) * 4 + 1]; };
        // Plane +V points upward; the first rows of the image must contain larger V.
        EXPECT_GT(at(*thumbnail, 20), 190);
        EXPECT_LT(at(*thumbnail, 44), 65);
        EXPECT_EQ(thumbnail->Pixels[(32 * 64 + 32) * 4], 255);
        texture.Mips[0].Pixels[0] = std::byte {0};
        for (auto& record : Records)
            if (record.Metadata.assetID == textureID)
                record = Store(
                    textureID, AssetType::Texture, std::get<std::vector<std::byte>>(EncodeTextureProduct(texture)));
        Provider->SetClosed(true);
        ASSERT_TRUE(std::holds_alternative<ProductChangeSet>(Assets->SetRegistry(Registry(Records))));
        EXPECT_FALSE(preview.RenderThumbnail(materialID, Options).has_value());
        Provider->SetClosed(false);
        ASSERT_TRUE(Frames(*Renderer->GetAssetCache(), [&] {
            auto status = Assets->Request<TextureAsset>(textureID);
            return status.IsReady() && Renderer->GetAssetCache()->GetTexture(textureID) != nullptr;
        }));
        thumbnail = preview.RenderThumbnail(materialID, Options);
        ASSERT_TRUE(thumbnail.has_value());
        EXPECT_EQ(thumbnail->Pixels[(32 * 64 + 32) * 4], 0);
        EXPECT_EQ(thumbnail->Pixels[(32 * 64 + 32) * 4 + 3], 255);
    }
    TEST_F(MaterialPreviewNativeGL, ImGuiRestoresHostileNativeStateIncludingBindingsAndBufferRanges)
    {
        MaterialPreviewRenderer preview(*Renderer);
        auto program = CreateInternalShader(*Renderer->GetDevice(),
                                            "HostileStateTest",
                                            "#version 450\nvoid main(){gl_Position=vec4(0);}",
                                            "#version 450\nlayout(location=0) out vec4 c;void main(){c=vec4(1);}");
        ASSERT_TRUE(std::holds_alternative<RenderHardware::Handle<RenderHardware::Shader>>(program));
        auto source = Tint({1, 0, 0, 1});
        auto result = preview.Render(Resolve(source, &Custom), &Custom, Options);
        auto id     = Material(source);
        ASSERT_TRUE(std::holds_alternative<Ref<const MaterialDataAsset>>(Assets->Load<MaterialDataAsset>(id)));
        ASSERT_TRUE(std::holds_alternative<Ref<const ShaderDataAsset>>(Assets->Load<ShaderDataAsset>(ShaderID)));
        auto thumbnailOptions = Options;
        thumbnailOptions.Size = {32, 48};
        EXPECT_TRUE(preview.RenderThumbnail(id, thumbnailOptions).has_value());
        NativeUIFrame ui;
        GLuint array, buffer, textures[2], sampler, framebuffer, renderbuffer;
        glGenVertexArrays(1, &array);
        glGenBuffers(1, &buffer);
        glGenTextures(2, textures);
        glGenSamplers(1, &sampler);
        glGenFramebuffers(1, &framebuffer);
        glGenRenderbuffers(1, &renderbuffer);
        glBindVertexArray(array);
        glBindBuffer(GL_UNIFORM_BUFFER, buffer);
        glBufferData(GL_UNIFORM_BUFFER, 4096, nullptr, GL_STATIC_DRAW);
        GLint alignment = 0;
        glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &alignment);
        for (GLuint point : {0u, 1u, 2u, 11u})
            glBindBufferRange(GL_UNIFORM_BUFFER, point, buffer, alignment, 256);
        std::array<uint8_t, 256> untouched;
        untouched.fill(0xa5);
        glBufferSubData(GL_UNIFORM_BUFFER, alignment, 256, untouched.data());
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, buffer);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, buffer);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, textures[0]);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, textures[1]);
        glBindSampler(0, sampler);
        glBindSampler(3, sampler);
        glActiveTexture(GL_TEXTURE9);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
        glUseProgram(NativeProbe::ProgramName(std::get<RenderHardware::Handle<RenderHardware::Shader>>(program)));
        glViewport(3, 5, 19, 23);
        glScissor(1, 2, 3, 4);
        glEnable(GL_SCISSOR_TEST);
        glEnable(GL_STENCIL_TEST);
        glEnable(GL_CULL_FACE);
        glEnable(GL_FRAMEBUFFER_SRGB);
        glEnable(GL_RASTERIZER_DISCARD);
        glEnable(GL_COLOR_LOGIC_OP);
        glEnable(GL_CLIP_DISTANCE0);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glEnable(GL_PRIMITIVE_RESTART);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_GREATER);
        glDepthMask(GL_FALSE);
        glDepthRange(.2, .7);
        glCullFace(GL_FRONT);
        glFrontFace(GL_CW);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glEnablei(GL_BLEND, 0);
        glBlendFuncSeparatei(0, GL_ONE, GL_ZERO, GL_DST_ALPHA, GL_SRC_ALPHA);
        glBlendEquationSeparatei(0, GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_SUBTRACT);
        glColorMaski(0, GL_FALSE, GL_TRUE, GL_FALSE, GL_FALSE);
        glPixelStorei(GL_PACK_ALIGNMENT, 8);
        glPixelStorei(GL_PACK_ROW_LENGTH, 17);
        glPixelStorei(GL_PACK_SKIP_ROWS, 2);
        glPixelStorei(GL_PACK_SKIP_PIXELS, 3);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 19);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 4);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 5);
        const auto snapshot = [&] {
            std::vector<GLint64> values;
            for (auto name : {GL_DRAW_FRAMEBUFFER_BINDING,
                              GL_READ_FRAMEBUFFER_BINDING,
                              GL_CURRENT_PROGRAM,
                              GL_VERTEX_ARRAY_BINDING,
                              GL_ARRAY_BUFFER_BINDING,
                              GL_UNIFORM_BUFFER_BINDING,
                              GL_RENDERBUFFER_BINDING,
                              GL_ACTIVE_TEXTURE,
                              GL_DEPTH_FUNC,
                              GL_CULL_FACE_MODE,
                              GL_FRONT_FACE,
                              GL_PIXEL_PACK_BUFFER_BINDING,
                              GL_PIXEL_UNPACK_BUFFER_BINDING,
                              GL_PACK_ALIGNMENT,
                              GL_PACK_ROW_LENGTH,
                              GL_PACK_SKIP_ROWS,
                              GL_PACK_SKIP_PIXELS,
                              GL_UNPACK_ALIGNMENT,
                              GL_UNPACK_ROW_LENGTH,
                              GL_UNPACK_SKIP_ROWS,
                              GL_UNPACK_SKIP_PIXELS})
            {
                GLint v;
                glGetIntegerv(name, &v);
                values.push_back(v);
            }
            for (auto name : {GL_VIEWPORT, GL_SCISSOR_BOX, GL_COLOR_WRITEMASK})
            {
                GLint v[4];
                glGetIntegerv(name, v);
                values.insert(values.end(), v, v + 4);
            }
            GLint polygon[2];
            glGetIntegerv(GL_POLYGON_MODE, polygon);
            values.insert(values.end(), polygon, polygon + 2);
            for (auto name : {GL_DEPTH_WRITEMASK,
                              GL_BLEND_SRC_RGB,
                              GL_BLEND_DST_RGB,
                              GL_BLEND_SRC_ALPHA,
                              GL_BLEND_DST_ALPHA,
                              GL_BLEND_EQUATION_RGB,
                              GL_BLEND_EQUATION_ALPHA})
            {
                GLint v;
                glGetIntegerv(name, &v);
                values.push_back(v);
            }
            for (auto cap : {GL_DEPTH_TEST,
                             GL_BLEND,
                             GL_SCISSOR_TEST,
                             GL_STENCIL_TEST,
                             GL_CULL_FACE,
                             GL_FRAMEBUFFER_SRGB,
                             GL_RASTERIZER_DISCARD,
                             GL_COLOR_LOGIC_OP,
                             GL_CLIP_DISTANCE0,
                             GL_POLYGON_OFFSET_FILL,
                             GL_PRIMITIVE_RESTART})
                values.push_back(glIsEnabled(cap));
            GLdouble range[2];
            glGetDoublev(GL_DEPTH_RANGE, range);
            for (double v : range)
                values.push_back(GLint64(v * 1000000));
            GLint active;
            glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
            for (GLuint unit = 0; unit < 16; ++unit)
            {
                glActiveTexture(GL_TEXTURE0 + unit);
                for (auto name : {GL_TEXTURE_BINDING_2D, GL_TEXTURE_BINDING_CUBE_MAP})
                {
                    GLint v;
                    glGetIntegerv(name, &v);
                    values.push_back(v);
                }
                GLint v;
                glGetIntegeri_v(GL_SAMPLER_BINDING, unit, &v);
                values.push_back(v);
            }
            glActiveTexture(active);
            for (GLuint point : {0u, 1u, 2u, 11u})
            {
                GLint v;
                glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, point, &v);
                values.push_back(v);
                for (auto name : {GL_UNIFORM_BUFFER_START, GL_UNIFORM_BUFFER_SIZE})
                {
                    GLint64 r;
                    glGetInteger64i_v(name, point, &r);
                    values.push_back(r);
                }
            }
            return values;
        };
        const auto before = snapshot();
        ui.Draw();
        EXPECT_EQ(snapshot(), before);
        ui.Draw();
        EXPECT_EQ(snapshot(), before);
        // Engine frame/object data held by the caller must also remain byte-for-byte intact.
        std::array<uint8_t, 256> after;
        glGetBufferSubData(GL_UNIFORM_BUFFER, alignment, 256, after.data());
        EXPECT_EQ(after, untouched);
        EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        for (auto name : {GL_PACK_ROW_LENGTH, GL_PACK_SKIP_ROWS, GL_PACK_SKIP_PIXELS})
            glPixelStorei(name, 0);
        // RHI rendering must still establish correct state after a hostile native UI caller.
        result = preview.Render(Resolve(source, &Custom), &Custom, Options);
        EXPECT_EQ(PreviewPixel(result.Texture), (std::array<uint8_t, 4> {255, 0, 0, 255}));
        glUseProgram(0);
        glBindVertexArray(0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteVertexArrays(1, &array);
        glDeleteBuffers(1, &buffer);
        glDeleteTextures(2, textures);
        glDeleteSamplers(1, &sampler);
        glDeleteFramebuffers(1, &framebuffer);
        glDeleteRenderbuffers(1, &renderbuffer);
    }
    TEST_P(MaterialPreviewGL, PreviewBetweenSceneSubmissionAndDrawPreservesQueueModeAndStats)
    {
        auto model = Quad(Color({1, 0, 0, 1}));
        Ready();
        Renderer->SetViewMode(SceneViewMode::Normals);
        Renderer->BeginScene(Info);
        Renderer->SubmitModel(model, glm::mat4(1));
        const auto              stats = Renderer->GetStats();
        MaterialPreviewRenderer preview(*Renderer);
        auto                    source = Tint({0, 1, 0, 1});
        auto                    result = preview.RenderImage(Resolve(source, &Custom), &Custom, Options);
        EXPECT_EQ(PreviewPixel(result.Target), (std::array<uint8_t, 4> {0, 255, 0, 255}));
        EXPECT_EQ(Renderer->GetStats().DrawCalls, stats.DrawCalls);
        Renderer->EndScene();
        EXPECT_EQ(Renderer->GetStats().DrawCalls, 1u);
        EXPECT_EQ(Pixel(), (std::array<uint8_t, 4> {128, 128, 255, 255}));
    }
    TEST_F(MaterialPreviewNativeGL, PublishedShaderReloadAndCacheEvictionDoNotRetainGpuPointers)
    {
        const auto material = Color({1, 0, 0, 1});
        Ready();
        MaterialPreviewRenderer preview(*Renderer);
        auto                    initial = preview.RenderThumbnail(material, Options);
        ASSERT_TRUE(initial.has_value());
        EXPECT_EQ(initial->Pixels[(32 * 64 + 32) * 4], 255);
        auto replacement = CompileTestMaterialShader(TestMaterialVertex, R"(#version 450
layout(set=1,binding=0,std140) uniform Parameters { vec4 Tint; } p;
layout(location=0) out vec4 color;
void main() { color=vec4(p.Tint.brg,1); }
)");
        for (auto& record : Records)
            if (record.Metadata.assetID == ShaderID)
                record = Store(
                    ShaderID, AssetType::Shader, std::get<std::vector<std::byte>>(EncodeShaderProduct(replacement)));
        Provider->SetClosed(true);
        ASSERT_TRUE(std::holds_alternative<ProductChangeSet>(Assets->SetRegistry(Registry(Records))));
        // The old program must not count as complete while a replacement layout is pending.
        EXPECT_FALSE(preview.RenderThumbnail(material, Options).has_value());
        Renderer->GetAssetCache()->BeginFrame(0);
        Provider->SetClosed(false);
        ASSERT_TRUE(Frames(*Renderer->GetAssetCache(),
                           [&] { return Renderer->GetAssetCache()->GetShader(ShaderID) != nullptr; }));
        auto changed = preview.RenderThumbnail(material, Options);
        ASSERT_TRUE(changed.has_value());
        EXPECT_EQ(changed->Pixels[(32 * 64 + 32) * 4], 0);
        EXPECT_EQ(changed->Pixels[(32 * 64 + 32) * 4 + 1], 255);
        EXPECT_EQ(changed->Pixels[(32 * 64 + 32) * 4 + 2], 0);
        EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
    }
    TEST_P(MaterialPreviewGL, CustomOpaqueOutputAlphaCannotMakeThePreviewTransparent)
    {
        // Project shaders may emit arbitrary alpha even in opaque mode; the editor image must stay opaque.
        ShaderData shader;
        shader.Code = CompileTestMaterialShader(TestMaterialVertex, R"(#version 450
layout(location=0) out vec4 color;
void main() { color=vec4(0,0,1,0.125); }
)");
        MaterialSource source;
        source.Shader = ShaderID;
        MaterialPreviewRenderer preview(*Renderer);
        auto                    result = preview.RenderImage(Resolve(source, &shader), &shader, Options);
        EXPECT_EQ(PreviewPixel(result.Target), (std::array<uint8_t, 4> {0, 0, 255, 255}));
    }
    TEST_P(MaterialPreviewGL, ThumbnailRejectsFailedTextureUploadDespiteAnOlderGpuTexture)
    {
        ShaderData shader;
        shader.Code          = CompileTestMaterialShader();
        auto        shaderID = Add(AssetType::Shader, EncodeShaderProduct(shader.Code));
        TextureData texture;
        texture.Width = texture.Height = 1;
        texture.Usage                  = TextureUsage::Data;
        texture.Mips                   = {{{std::byte {255}, std::byte {0}, std::byte {0}, std::byte {255}}}};
        auto           textureID       = Add(AssetType::Texture, EncodeTextureProduct(texture));
        MaterialSource source;
        source.Shader               = shaderID;
        source.Parameters           = {{"Tint", {1, 1, 1, 1}}, {"Strength", {1}}};
        source.Textures["u_Albedo"] = textureID;
        auto materialID             = Add(AssetType::Material, EncodeMaterialProduct(Resolve(source, &shader)));
        ASSERT_TRUE(std::holds_alternative<Ref<const MaterialDataAsset>>(Assets->Load<MaterialDataAsset>(materialID)));
        ASSERT_TRUE(std::holds_alternative<Ref<const ShaderDataAsset>>(Assets->Load<ShaderDataAsset>(shaderID)));
        ASSERT_TRUE(std::holds_alternative<Ref<const TextureAsset>>(Assets->Load<TextureAsset>(textureID)));
        MaterialPreviewRenderer preview(*Renderer);
        auto                    initial = preview.RenderThumbnail(materialID, Options);
        ASSERT_TRUE(initial.has_value());
        EXPECT_EQ(initial->Pixels[(32 * 64 + 32) * 4], 255);
        // A valid array Product is CPU-ready but unsupported by the 2D GPU uploader.
        // Resolve keeps the old red texture; its existence must not mark the new version complete.
        texture.Kind   = TextureKind::Array2D;
        texture.Layers = 2;
        texture.Mips[0].Pixels.resize(8, std::byte {255});
        for (auto& record : Records)
            if (record.Metadata.assetID == textureID)
                record = Store(
                    textureID, AssetType::Texture, std::get<std::vector<std::byte>>(EncodeTextureProduct(texture)));
        ASSERT_TRUE(std::holds_alternative<ProductChangeSet>(Assets->SetRegistry(Registry(Records))));
        ASSERT_TRUE(std::holds_alternative<Ref<const TextureAsset>>(Assets->Load<TextureAsset>(textureID)));
        EXPECT_FALSE(preview.RenderThumbnail(materialID, Options).has_value());
        EXPECT_FALSE(preview.RenderImage(materialID, Options).Complete);
    }
    TEST_F(MaterialPreviewNativeGL, PreviewReplacementsAreReleasedWithoutBeginFrame)
    {
        ShaderData shader;
        shader.Code          = CompileTestMaterialShader();
        auto        shaderID = Add(AssetType::Shader, EncodeShaderProduct(shader.Code));
        TextureData texture;
        texture.Width = texture.Height = 1;
        texture.Usage                  = TextureUsage::Data;
        texture.Mips                   = {{{std::byte {255}, std::byte {0}, std::byte {0}, std::byte {255}}}};
        auto           textureID       = Add(AssetType::Texture, EncodeTextureProduct(texture));
        MaterialSource source;
        source.Shader               = shaderID;
        source.Parameters           = {{"Tint", {1, 1, 1, 1}}, {"Strength", {1}}};
        source.Textures["u_Albedo"] = textureID;
        auto materialID             = Add(AssetType::Material, EncodeMaterialProduct(Resolve(source, &shader)));
        ASSERT_TRUE(std::holds_alternative<Ref<const MaterialDataAsset>>(Assets->Load<MaterialDataAsset>(materialID)));
        ASSERT_TRUE(std::holds_alternative<Ref<const ShaderDataAsset>>(Assets->Load<ShaderDataAsset>(shaderID)));
        ASSERT_TRUE(std::holds_alternative<Ref<const TextureAsset>>(Assets->Load<TextureAsset>(textureID)));
        auto&                   cache = *Renderer->GetAssetCache();
        MaterialPreviewRenderer preview(*Renderer);
        ASSERT_TRUE(preview.Render(materialID, Options).Complete);
        // A hidden viewport never calls BeginFrame. Each render entry point must
        // release unbound replaced objects, including after nested thumbnail guards.
        for (int path = 0; path < 3; ++path)
        {
            SCOPED_TRACE(path);
            const auto oldProgram         = NativeProgram(*cache.GetShader(shaderID));
            const auto oldBuffer          = NativeBuffer(cache.GetMaterialBindings(materialID)->Blocks.front().second);
            const auto oldTexture         = cache.GetTexture(textureID)->GetImGuiTextureID();
            source.Parameters["Strength"] = {0.5f + 0.1f * path};
            shader.Code            = CompileTestMaterialShader(TestMaterialVertex,
                                                               R"(#version 450
layout(set=1,binding=0,std140) uniform Parameters { vec4 Tint; float Strength; int Steps; vec2 Offsets[2]; } p;
layout(set=1,binding=1) uniform sampler2D u_Albedo;
layout(location=0) in vec2 v_UV;
layout(location=0) out vec4 color;
void main() { color=p.Tint*p.Strength*texture(u_Albedo,v_UV)*vec4(1, )" +
                                                                   std::to_string(0.9f - 0.1f * path) + ", 1, 1); }\n");
            texture.Mips[0].Pixels = {std::byte {0}, std::byte(255 - 50 * path), std::byte {0}, std::byte {255}};
            for (auto& record : Records)
            {
                const auto id = record.Metadata.assetID;
                if (id == shaderID)
                    record = Store(
                        id, AssetType::Shader, std::get<std::vector<std::byte>>(EncodeShaderProduct(shader.Code)));
                if (id == textureID)
                    record =
                        Store(id, AssetType::Texture, std::get<std::vector<std::byte>>(EncodeTextureProduct(texture)));
                if (id == materialID)
                    record = Store(id,
                                   AssetType::Material,
                                   std::get<std::vector<std::byte>>(EncodeMaterialProduct(Resolve(source, &shader))));
            }
            ASSERT_TRUE(std::holds_alternative<ProductChangeSet>(Assets->SetRegistry(Registry(Records))));
            ASSERT_TRUE(
                std::holds_alternative<Ref<const MaterialDataAsset>>(Assets->Load<MaterialDataAsset>(materialID)));
            ASSERT_TRUE(std::holds_alternative<Ref<const ShaderDataAsset>>(Assets->Load<ShaderDataAsset>(shaderID)));
            ASSERT_TRUE(std::holds_alternative<Ref<const TextureAsset>>(Assets->Load<TextureAsset>(textureID)));
            if (path == 0)
                ASSERT_TRUE(preview.Render(materialID, Options).Complete);
            else if (path == 1)
                ASSERT_TRUE(preview.RenderThumbnail(materialID, Options).has_value());
            else
                ASSERT_TRUE(preview.Render(Resolve(source, &shader), &shader, Options).Complete);
            EXPECT_FALSE(glIsTexture(oldTexture));
            // Resolved previews own their program/UBOs; only published previews
            // replace the cache's shader and material bindings.
            if (path < 2)
            {
                EXPECT_FALSE(glIsProgram(oldProgram));
                EXPECT_FALSE(glIsBuffer(oldBuffer));
            }
            EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
        }
    }
    TEST_F(MaterialPreviewNativeGL, ReimportRetainsRecordedProgramsTexturesAndUniformBuffersUntilNextFrame)
    {
        ShaderData shader;
        shader.Code          = CompileTestMaterialShader();
        auto        shaderID = Add(AssetType::Shader, EncodeShaderProduct(shader.Code));
        TextureData texture;
        texture.Width = texture.Height = 1;
        texture.Usage                  = TextureUsage::Data;
        texture.Mips                   = {{{std::byte {255}, std::byte {0}, std::byte {0}, std::byte {255}}}};
        auto           textureID       = Add(AssetType::Texture, EncodeTextureProduct(texture));
        MaterialSource source;
        source.Shader               = shaderID;
        source.Parameters           = {{"Tint", {1, 1, 1, 1}}, {"Strength", {1}}};
        source.Textures["u_Albedo"] = textureID;
        auto materialID             = Add(AssetType::Material, EncodeMaterialProduct(Resolve(source, &shader)));
        ASSERT_TRUE(std::holds_alternative<Ref<const MaterialDataAsset>>(Assets->Load<MaterialDataAsset>(materialID)));
        ASSERT_TRUE(std::holds_alternative<Ref<const ShaderDataAsset>>(Assets->Load<ShaderDataAsset>(shaderID)));
        ASSERT_TRUE(std::holds_alternative<Ref<const TextureAsset>>(Assets->Load<TextureAsset>(textureID)));
        auto& cache = *Renderer->GetAssetCache();
        // A recorded draw owns immutable descriptor/program snapshots across reimport.
        const auto* oldBindings = cache.GetMaterialBindings(materialID);
        ASSERT_NE(oldBindings, nullptr);
        const auto* oldShader = cache.GetShader(shaderID);
        ASSERT_NE(oldShader, nullptr);
        auto          oldGroup   = cache.GetMaterialGroup(*oldShader, *oldBindings);
        auto          oldProgram = oldShader->Program;
        NativeUIFrame ui;
        NativeBind(*oldShader);
        for (const auto& [binding, buffer] : oldBindings->Blocks)
            NativeBind(buffer, MaterialBlockPoint(*oldShader, binding));
        ASSERT_TRUE(BindMaterialTextures(cache, *oldShader, *oldBindings));
        GLint program, ubo, image;
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, 2, &ubo);
        glActiveTexture(GL_TEXTURE3);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &image);
        std::array<float, 4> oldTint;
        glBindBuffer(GL_UNIFORM_BUFFER, ubo);
        glGetBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(oldTint), oldTint.data());
        shader.Code               = CompileTestMaterialShader(TestMaterialVertex, R"(#version 450
layout(set=1,binding=0,std140) uniform Parameters { vec4 Tint; float Strength; int Steps; vec2 Offsets[2]; } p;
layout(set=1,binding=1) uniform sampler2D u_Albedo;
layout(location=0) in vec2 v_UV;
layout(location=0) out vec4 color;
void main() { color=(p.Tint*p.Strength*texture(u_Albedo,v_UV)).bgra; }
)");
        source.Parameters["Tint"] = {0, 1, 0, 1};
        texture.Mips[0].Pixels    = {std::byte {0}, std::byte {255}, std::byte {0}, std::byte {255}};
        for (auto& record : Records)
        {
            auto id = record.Metadata.assetID;
            if (id == shaderID)
                record =
                    Store(id, AssetType::Shader, std::get<std::vector<std::byte>>(EncodeShaderProduct(shader.Code)));
            if (id == textureID)
                record = Store(id, AssetType::Texture, std::get<std::vector<std::byte>>(EncodeTextureProduct(texture)));
            if (id == materialID)
                record = Store(id,
                               AssetType::Material,
                               std::get<std::vector<std::byte>>(EncodeMaterialProduct(Resolve(source, &shader))));
        }
        ASSERT_TRUE(std::holds_alternative<ProductChangeSet>(Assets->SetRegistry(Registry(Records))));
        ASSERT_TRUE(std::holds_alternative<Ref<const MaterialDataAsset>>(Assets->Load<MaterialDataAsset>(materialID)));
        ASSERT_TRUE(std::holds_alternative<Ref<const ShaderDataAsset>>(Assets->Load<ShaderDataAsset>(shaderID)));
        ASSERT_TRUE(std::holds_alternative<Ref<const TextureAsset>>(Assets->Load<TextureAsset>(textureID)));
        MaterialPreviewRenderer preview(*Renderer);
        auto                    result = preview.Render(materialID, Options);
        ASSERT_TRUE(result.Complete);
        glUseProgram(program);
        glBindBufferBase(GL_UNIFORM_BUFFER, 2, ubo);
        glBindBuffer(GL_UNIFORM_BUFFER, ubo);
        glBindTexture(GL_TEXTURE_2D, image);
        ui.Draw();
        GLint restored;
        glGetIntegerv(GL_CURRENT_PROGRAM, &restored);
        EXPECT_EQ(restored, program);
        glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, 2, &restored);
        EXPECT_EQ(restored, ubo);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &restored);
        EXPECT_EQ(restored, image);
        EXPECT_TRUE(glIsTexture(image));
        EXPECT_TRUE(glIsProgram(program));
        EXPECT_TRUE(glIsBuffer(ubo));
        std::array<float, 4> restoredTint;
        glGetBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(restoredTint), restoredTint.data());
        EXPECT_EQ(restoredTint, oldTint);
        EXPECT_EQ(PreviewPixel(result.Texture), (std::array<uint8_t, 4> {0, 255, 0, 255}));
        EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
        glUseProgram(0);
        glBindBufferBase(GL_UNIFORM_BUFFER, 2, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        ui.End();
        oldGroup.reset();
        oldProgram.reset();
        cache.BeginFrame();
        // Deferral must be bounded; holding every replaced object forever leaks on each edit/reimport.
        EXPECT_FALSE(glIsProgram(program));
        EXPECT_FALSE(glIsTexture(image));
        EXPECT_FALSE(glIsBuffer(ubo));
    }
    INSTANTIATE_TEST_SUITE_P(Backends,
                             MaterialSceneGL,
                             testing::Values(GpuTestBackend::OpenGL, GpuTestBackend::Vulkan),
                             [](const testing::TestParamInfo<GpuTestBackend>& info) {
                                 return info.param == GpuTestBackend::OpenGL ? "OpenGL" : "Vulkan";
                             });
    INSTANTIATE_TEST_SUITE_P(Backends,
                             MultiPassSceneGL,
                             testing::Values(GpuTestBackend::OpenGL, GpuTestBackend::Vulkan),
                             [](const testing::TestParamInfo<GpuTestBackend>& info) {
                                 return info.param == GpuTestBackend::OpenGL ? "OpenGL" : "Vulkan";
                             });
    INSTANTIATE_TEST_SUITE_P(Backends,
                             StandardMaterialGL,
                             testing::Values(GpuTestBackend::OpenGL, GpuTestBackend::Vulkan),
                             [](const testing::TestParamInfo<GpuTestBackend>& info) {
                                 return info.param == GpuTestBackend::OpenGL ? "OpenGL" : "Vulkan";
                             });
    INSTANTIATE_TEST_SUITE_P(Backends,
                             MaterialPreviewGL,
                             testing::Values(GpuTestBackend::OpenGL, GpuTestBackend::Vulkan),
                             [](const testing::TestParamInfo<GpuTestBackend>& info) {
                                 return info.param == GpuTestBackend::OpenGL ? "OpenGL" : "Vulkan";
                             });
    INSTANTIATE_TEST_SUITE_P(Backends,
                             RenderAssetCacheGPU,
                             testing::Values(GpuTestBackend::OpenGL, GpuTestBackend::Vulkan),
                             [](const testing::TestParamInfo<GpuTestBackend>& info) {
                                 return info.param == GpuTestBackend::OpenGL ? "OpenGL" : "Vulkan";
                             });
} // namespace
