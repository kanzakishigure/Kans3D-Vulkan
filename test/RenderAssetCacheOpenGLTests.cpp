#include <glad/glad.h>
#include <gtest/gtest.h>
#ifdef KANS_TEST_USE_EGL
#include <EGL/egl.h>
#include <EGL/eglext.h>
#else
#include <GLFW/glfw3.h>
#endif
#include <chrono>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>
#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/sinks/ostream_sink.h>
#include "Kans3D/Asset/Importer/MaterialSource.h"
#include "Kans3D/Asset/Product/ContentDigest.h"
#include "Kans3D/Asset/Product/MeshProduct.h"
#include "Kans3D/Asset/Product/ModelProduct.h"
#include "Kans3D/Asset/Product/ShaderProduct.h"
#include "Kans3D/Asset/Product/TextureProduct.h"
#include "Kans3D/Asset/Runtime/StandardShader.h"
#include "Kans3D/Core/Log/Log.h"
#include "Kans3D/Platform/OpenGL/OpenGLRHI.h"
#include "Kans3D/Platform/OpenGL/OpenGLShader.h"
#include "Kans3D/Renderer/RHI/RendererAPI.h"
#include "Kans3D/Renderer/Resource/RenderAssetCache.h"
#include "Kans3D/Renderer/Resource/SelectionTools.h"
#include "Kans3D/Renderer/SceneRenderer.h"
#include "MaterialShaderTestUtilities.h"

namespace
{
    using namespace Kans;
    using namespace std::chrono_literals;

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

    class RenderAssetCacheGL : public testing::Test
    {
    protected:
        void SetUp() override
        {
            if (!Log::GetCoreLogger())
                Log::Init();
#ifdef KANS_TEST_USE_EGL
            display = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
            ASSERT_NE(display, EGL_NO_DISPLAY);
            ASSERT_TRUE(eglInitialize(display, nullptr, nullptr));
            ASSERT_TRUE(eglBindAPI(EGL_OPENGL_API));
            const EGLint configAttributes[] = {
                EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE};
            EGLConfig config;
            EGLint    count = 0;
            ASSERT_TRUE(eglChooseConfig(display, configAttributes, &config, 1, &count));
            ASSERT_GT(count, 0);
            const EGLint attributes[] = {EGL_CONTEXT_MAJOR_VERSION,
                                         4,
                                         EGL_CONTEXT_MINOR_VERSION,
                                         3,
                                         EGL_CONTEXT_OPENGL_PROFILE_MASK,
                                         EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                                         EGL_NONE};
            context                   = eglCreateContext(display, config, EGL_NO_CONTEXT, attributes);
            ASSERT_NE(context, EGL_NO_CONTEXT);
            const EGLint surfaceAttributes[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
            surface                          = eglCreatePbufferSurface(display, config, surfaceAttributes);
            ASSERT_NE(surface, EGL_NO_SURFACE);
            ASSERT_TRUE(eglMakeCurrent(display, surface, surface, context));
            ASSERT_TRUE(gladLoadGLLoader(reinterpret_cast<GLADloadproc>(eglGetProcAddress)));
            OpenGLRHI::EnsureDSAFunctionsLoaded(reinterpret_cast<GLADloadproc>(eglGetProcAddress));
#else
            ASSERT_TRUE(glfwInit());
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            window = glfwCreateWindow(16, 16, "RenderAssetCache test", nullptr, nullptr);
            ASSERT_NE(window, nullptr);
            glfwMakeContextCurrent(window);
            ASSERT_TRUE(gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)));
            OpenGLRHI::EnsureDSAFunctionsLoaded(reinterpret_cast<GLADloadproc>(glfwGetProcAddress));
#endif
            previousBackend = RendererAPI::Current();
            RendererAPI::SetBackEnd(RendererAPIType::OPENGL);
            ASSERT_EQ(glGetError(), GL_NO_ERROR);
        }

        void TearDown() override
        {
            Provider->SetClosed(false);
            RendererAPI::SetBackEnd(previousBackend);
#ifdef KANS_TEST_USE_EGL
            if (display != EGL_NO_DISPLAY)
            {
                eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
                if (surface != EGL_NO_SURFACE)
                    eglDestroySurface(display, surface);
                if (context != EGL_NO_CONTEXT)
                    eglDestroyContext(display, context);
                eglTerminate(display);
            }
#else
            if (window)
                glfwDestroyWindow(window);
            glfwTerminate();
#endif
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
        RendererAPIType    previousBackend {};
#ifdef KANS_TEST_USE_EGL
        EGLDisplay display = EGL_NO_DISPLAY;
        EGLContext context = EGL_NO_CONTEXT;
        EGLSurface surface = EGL_NO_SURFACE;
#else
        GLFWwindow* window = nullptr;
#endif
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
        mesh.VertexArray->Bind();
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
        mesh.VertexArray->Unbind();
        EXPECT_EQ(glGetError(), GL_NO_ERROR);

        data.Indices[0] = 100;
        EXPECT_EQ(std::get<AssetError>(UploadMesh(data)).Code, AssetErrorCode::InvalidData);
    }

    TEST_F(RenderAssetCacheGL, ResourcesAppearWhenLoadedAndInvalidIDsReturnNothing)
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
        EXPECT_EQ(glGetError(), GL_NO_ERROR);
    }

    TEST_F(RenderAssetCacheGL, ReimportedAssetKeepsLastUploadUntilReplacementIsReady)
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
        EXPECT_EQ(glGetError(), GL_NO_ERROR);
    }

    TEST_F(RenderAssetCacheGL, ModelInstancesResolveMeshesAndUnusedEntriesAreEvicted)
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
        EXPECT_EQ(glGetError(), GL_NO_ERROR);
    }

    class StandardMaterialGL : public RenderAssetCacheGL
    {
    protected:
        void SetUp() override
        {
            RenderAssetCacheGL::SetUp();
            Assets = CreateScope<AssetManager>(Provider, Registry({}));
            Cache  = CreateScope<RenderAssetCache>(*Assets);
            glGenFramebuffers(1, &Target);
            glBindFramebuffer(GL_FRAMEBUFFER, Target);
            glGenTextures(1, &Color);
            glBindTexture(GL_TEXTURE_2D, Color);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 16, 16, 0, GL_RGBA, GL_FLOAT, nullptr);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, Color, 0);
            ASSERT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GLenum(GL_FRAMEBUFFER_COMPLETE));
            glViewport(0, 0, 16, 16);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glDisable(GL_BLEND);
            glDisable(GL_FRAMEBUFFER_SRGB);
            glGenTextures(2, Environment);
            const float black[4] = {0, 0, 0, 1};
            for (uint32_t unit = 0; unit < 2; ++unit)
            {
                glActiveTexture(GL_TEXTURE0 + unit);
                glBindTexture(GL_TEXTURE_CUBE_MAP, Environment[unit]);
                for (uint32_t face = 0; face < 6; ++face)
                    glTexImage2D(
                        GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA32F, 1, 1, 0, GL_RGBA, GL_FLOAT, black);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            }
            Frame.CameraPosition = {0, 0, 2, 1};
            for (auto& light : Frame.LightPositions)
                light = {0, 0, 1, 1};
            FrameBuffer  = CreateScope<GpuUniformBuffer>(sizeof(Frame));
            ObjectBuffer = CreateScope<GpuUniformBuffer>(sizeof(Object));
            FrameBuffer->Bind(GpuShader::FrameBlockPoint);
            ObjectBuffer->Bind(GpuShader::ObjectBlockPoint);
        }
        void TearDown() override
        {
            Cache.reset();
            Assets.reset();
            FrameBuffer.reset();
            ObjectBuffer.reset();
            glDeleteTextures(2, Environment);
            glDeleteTextures(1, &Color);
            glDeleteFramebuffers(1, &Target);
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
            Cache->GetTexture(id)->Bind(12);
            glActiveTexture(GL_TEXTURE12);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
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
            shader->Bind();
            FrameBuffer->Update(&Frame, sizeof(Frame));
            ObjectBuffer->Update(&Object, sizeof(Object));
            for (const auto& [binding, buffer] : bindings.Blocks)
                buffer.Bind(shader->MaterialBlockPoints().at(binding));
            Cache->BindMaterialTextures(*shader, bindings);
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
            std::get<GpuMesh>(mesh).VertexArray->Bind();
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
            EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
            std::array<float, 4> pixel {};
            glReadPixels(x, 8, 1, 1, GL_RGBA, GL_FLOAT, pixel.data());
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
        GLuint                     Target = 0, Color = 0, Environment[2] {};
    };

    TEST_F(StandardMaterialGL, PackingFailureLogsOnceDrawsStandardAndRetriesEitherChangedSource)
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

    TEST_F(StandardMaterialGL, UnassignedNormalIsGeometricAndUnassignedEmissionIsDark)
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

    TEST_F(StandardMaterialGL, VertexColorIsOptInAndStrengthOneAppliesIt)
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

    TEST_F(StandardMaterialGL, TangentDirectionHandednessAndDerivativeFallbackOrientNormalMap)
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

    TEST_F(StandardMaterialGL, UvTilingRepeatsAndOffsetIsAppliedAfterScaling)
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

    TEST_F(StandardMaterialGL, EmissiveFactorAndMapMultiplyBeforeToneMapping)
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

    TEST_F(StandardMaterialGL, StandardFactorsShadeWithSceneLightsAndGltfPackedChannels)
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

    TEST_F(StandardMaterialGL, OcclusionRedChannelAndStrengthOnlyAttenuateIbl)
    {
        // Known diffuse IBL: dielectric kD=0.96, irradiance=0.5, albedo=0.25.
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, Environment[0]);
        const float irradiance[4] = {0.5f, 0.5f, 0.5f, 1};
        for (uint32_t face = 0; face < 6; ++face)
            glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, 0, 0, 1, 1, GL_RGBA, GL_FLOAT, irradiance);
        MaterialSource source;
        source.Parameters["BaseColor"]         = {0.25, 0.25, 0.25, 1};
        source.Textures["OcclusionMap"]        = Image({{0.2f, 0.7f, 0.9f, 1}});
        source.Parameters["OcclusionStrength"] = {0.5};
        const auto occluded                    = Draw(source);
        EXPECT_NEAR(occluded[0], Encode(0.96f * 0.5f * 0.25f * 0.6f), 2e-6f);
        source.Parameters["OcclusionStrength"] = {0};
        const auto full                        = Draw(source);
        EXPECT_NEAR(full[0], Encode(0.96f * 0.5f * 0.25f), 2e-6f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, Environment[0]);
        const float black[4] = {0, 0, 0, 1};
        for (uint32_t face = 0; face < 6; ++face)
            glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, 0, 0, 1, 1, GL_RGBA, GL_FLOAT, black);
        for (auto& light : Frame.LightColors)
            light = {2, 2, 2, 1};
        const auto direct                      = Draw(source);
        source.Parameters["OcclusionStrength"] = {1};
        const auto directOccluded              = Draw(source);
        EXPECT_NEAR(direct[0], directOccluded[0], 2e-6f);
    }

    TEST_F(RenderAssetCacheGL, CustomSamplerFallbackBindsBlackAndNormalThroughPublishedProduct)
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
            cache.BindMaterialTextures(std::get<GpuShader>(program), {});
            const auto unit = std::get<GpuShader>(program).MaterialSamplerUnits().at(1);
            glActiveTexture(GL_TEXTURE0 + unit);
            std::array<float, 4> pixel {};
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixel.data());
            const auto expected = role == ShaderTextureFallback::Black ? std::array<float, 4> {0, 0, 0, 1} :
                                                                         std::array<float, 4> {0.5f, 0.5f, 1, 1};
            EXPECT_EQ(pixel, expected);
        }
    }

    TEST_F(RenderAssetCacheGL, MissingFailedAndDebugCustomShadersUseStandardBindings)
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
        legacy->Blocks.at(0).second.Bind(GpuShader::FirstMaterialBlockPoint);
        GLint buffer = 0;
        glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, GpuShader::FirstMaterialBlockPoint, &buffer);
        glBindBuffer(GL_UNIFORM_BUFFER, GLuint(buffer));
        float red = 0;
        glGetBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(red), &red);
        EXPECT_FLOAT_EQ(red, 0.125f);
    }

    // Draws a quad with a Shader material the way SceneRenderer does: engine data in
    // set 0, the material's packed blocks and textures in set 1.
    TEST_F(RenderAssetCacheGL, ShaderMaterialDrawsWithItsPackedParametersAndTextures)
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

        constexpr int Size        = 16;
        GLuint        framebuffer = 0, color = 0;
        glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glGenTextures(1, &color);
        glBindTexture(GL_TEXTURE_2D, color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, Size, Size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        ASSERT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GLenum(GL_FRAMEBUFFER_COMPLETE));
        glViewport(0, 0, Size, Size);

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
        GpuUniformBuffer frameBuffer(sizeof(frame), &frame), objectBuffer(sizeof(object), &object);
        frameBuffer.Bind(GpuShader::FrameBlockPoint);
        objectBuffer.Bind(GpuShader::ObjectBlockPoint);
        const auto draw = [&](AssetID materialID) -> std::array<uint8_t, 4> {
            const auto* bindings = cache.GetMaterialBindings(materialID);
            program->Bind();
            for (const auto& [binding, buffer] : bindings->Blocks)
                buffer.Bind(program->MaterialBlockPoints().at(binding));
            cache.BindMaterialTextures(*program, *bindings);
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            mesh.VertexArray->Bind();
            glDrawElements(GL_TRIANGLES, GLsizei(mesh.IndexCount), GL_UNSIGNED_INT, nullptr);
            EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
            std::array<uint8_t, Size * Size * 4> pixels {};
            glReadPixels(0, 0, Size, Size, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
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

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteTextures(1, &color);
        glDeleteFramebuffers(1, &framebuffer);
    }

    TEST_F(RenderAssetCacheGL, ShaderOutsideTheMaterialContractIsNotCompiled)
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

    // The editor's sample material shaders cross-compile and link on OpenGL.
    TEST_F(RenderAssetCacheGL, EditorSampleShadersLinkOnOpenGL)
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

    TEST_F(RenderAssetCacheGL, SceneSubmissionUsesStandardAndCustomShadersInLitDebugAndPreviewPasses)
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
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glViewport(0, 0, 16, 16);
        const auto read = [](int x) {
            std::array<uint8_t, 4> pixel {};
            glReadPixels(x, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
            return pixel;
        };
        renderer.BeginScene(info);
        renderer.SubmitModel(component, glm::mat4(1));
        EXPECT_GT(read(4)[0], 150);
        EXPECT_EQ(read(12)[1], 255); // Custom shader still runs in the lit view.
        EXPECT_EQ(read(12)[0], 0);
        EXPECT_EQ(renderer.GetStats().DrawCalls, 2u);
        EXPECT_EQ(renderer.GetStats().Triangles, 4u);
        renderer.EndScene();
        renderer.SetViewMode(SceneViewMode::BaseColor);
        renderer.BeginScene(info);
        renderer.SubmitModel(component, glm::mat4(1));
        EXPECT_EQ(read(4), (std::array<uint8_t, 4> {255, 0, 0, 255}));
        EXPECT_EQ(read(12), (std::array<uint8_t, 4> {255, 255, 255, 255}));
        renderer.EndScene();
        renderer.SetViewMode(SceneViewMode::Normals);
        renderer.BeginScene(info);
        renderer.SubmitModel(component, glm::mat4(1));
        for (int x : {4, 12})
        {
            const auto pixel = read(x);
            EXPECT_NEAR(pixel[0], 128, 1); // RGBA8 quantization of 0.5.
            EXPECT_NEAR(pixel[1], 128, 1);
            EXPECT_EQ(pixel[2], 255);
        }
        renderer.SetPreview(component, glm::mat4(1));
        renderer.EndScene();
        EXPECT_EQ(renderer.GetStats().DrawCalls, 4u); // Preview submits both parts through the lit path.
        renderer.SetViewMode(SceneViewMode::Wireframe);
        renderer.BeginScene(info);
        renderer.SubmitModel(component, glm::mat4(1));
        GLint polygon[2] {};
        glGetIntegerv(GL_POLYGON_MODE, polygon);
        EXPECT_EQ(polygon[0], GLint(GL_FILL));
        EXPECT_LT(read(2)[0], 50); // Interior away from the triangulation diagonal stays clear.
        renderer.EndScene();
        std::vector<MeshDraw> draws;
        renderer.AppendModelDraws(component, glm::mat4(1), 7, draws);
        ASSERT_EQ(draws.size(), 2u);
        EXPECT_EQ(renderer.Pick(draws, glm::mat4(1), {4, 8}), 7u);
        EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
    }

    class SurfacePickerGL : public RenderAssetCacheGL, public testing::WithParamInterface<bool>
    {
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
        quad.Indices = {0, 1, 2, 2, 3, 0};
        auto uploaded = UploadMesh(quad);
        ASSERT_TRUE(std::holds_alternative<GpuMesh>(uploaded));
        const auto& mesh = std::get<GpuMesh>(uploaded);
        const bool perspective = GetParam();
        const glm::mat4 projection = perspective ? glm::perspective(glm::radians(90.0f), 2.0f, 1.0f, 9.0f) :
                                                   glm::ortho(-8.0f, 8.0f, -4.0f, 4.0f, 1.0f, 9.0f);
        const glm::mat4 viewProjection = projection * glm::translate(glm::mat4(1.0f), {-10, -20, -30});
        const MeshDraw near {&mesh, glm::translate(glm::mat4(1.0f), {10, 20, 26}), 7};
        const MeshDraw far {&mesh, glm::translate(glm::mat4(1.0f), {10, 20, 24}), 9};

        // Picking must establish its depth state even after other render passes.
        glDepthMask(GL_FALSE);
        glDepthFunc(GL_GREATER);
        glDepthRange(0.25, 0.75);
        glClearDepth(0.0);
        EntityPicker picker;
        const auto hit = picker.PickSurface({near, far}, viewProjection, {32, 16}, {19, 5});
        ASSERT_EQ(hit.ID, 7u);
        ASSERT_TRUE(hit.Position);
        // NDC=(7/32,-5/16); at z=-4 both views span 16x8. Catches +0.5,
        // flipped Y, viewport aspect, depth range mapping, and missing inverse view.
        // 24-bit depth at these near/far planes gives <2e-6 world error; allow 2e-5 for matrix arithmetic.
        EXPECT_NEAR(hit.Position->x, 11.75f, 2e-5f);
        EXPECT_NEAR(hit.Position->y, 18.75f, 2e-5f);
        EXPECT_NEAR(hit.Position->z, 26.0f, 2e-5f);
        GLboolean depthMask = GL_TRUE;
        GLint depthFunction = 0;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        glGetIntegerv(GL_DEPTH_FUNC, &depthFunction);
        EXPECT_EQ(depthMask, GL_FALSE);
        EXPECT_EQ(depthFunction, GL_GREATER);

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
        EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
    }

    INSTANTIATE_TEST_SUITE_P(Projections, SurfacePickerGL, testing::Values(true, false),
                            [](const testing::TestParamInfo<bool>& info) {
                                return info.param ? "Perspective" : "Orthographic";
                            });

    // Picking reads the ID under a pixel; the outline borders a silhouette.
    TEST_F(RenderAssetCacheGL, PickerFindsMeshesAndOutlineBordersThem)
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
        EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));

        GLuint framebuffer = 0, color = 0;
        glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glGenTextures(1, &color);
        glBindTexture(GL_TEXTURE_2D, color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, Size, Size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        glViewport(0, 0, Size, Size);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        SelectionOutline outline;
        outline.Draw(draws, glm::mat4(1.0f), {Size, Size}, {1, 0, 0, 1}, 2.0f);
        EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
        // The outline leaves the target bound.
        GLint bound = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &bound);
        EXPECT_EQ(GLuint(bound), framebuffer);
        std::array<uint8_t, Size * Size * 4> pixels {};
        glReadPixels(0, 0, Size, Size, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        const auto red = [&](int x, int y) { return pixels[(y * Size + x) * 4]; };
        EXPECT_EQ(red(9, 8), 255);  // Border just right of the quad.
        EXPECT_EQ(red(14, 8), 0);   // Far from it.
        EXPECT_GT(red(4, 8), 0);    // Faint fill inside.
        EXPECT_LT(red(4, 8), 80);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteTextures(1, &color);
        glDeleteFramebuffers(1, &framebuffer);
    }
} // namespace
