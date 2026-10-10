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
#include "Kans3D/Project/Project.h"
#include "Kans3D/Renderer/RHI/RendererAPI.h"
#include "Kans3D/Renderer/Resource/RenderAssetCache.h"
#include "Kans3D/Renderer/Resource/SelectionTools.h"
#include "Kans3D/Renderer/SceneRenderer.h"
#include "Kans3D/Scene/Entity.h"
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
        const auto read = [&](int x) {
            // Model submissions are consumed at EndScene; inspect the scene texture after unbinding it.
            std::array<uint8_t, 16 * 16 * 4> image {};
            glBindTexture(GL_TEXTURE_2D, renderer.GetOutput(0));
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.data());
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
        GLint polygon[2] {};
        glGetIntegerv(GL_POLYGON_MODE, polygon);
        EXPECT_EQ(polygon[0], GLint(GL_FILL));
        EXPECT_LT(read(2)[0], 50); // Interior away from the triangulation diagonal stays clear.
        std::vector<MeshDraw> draws;
        renderer.AppendModelDraws(component, glm::mat4(1), 7, draws);
        ASSERT_EQ(draws.size(), 2u);
        EXPECT_EQ(renderer.Pick(draws, glm::mat4(1), {4, 8}), 7u);
        EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
    }

    TEST_F(RenderAssetCacheGL, ImportedRemappedModelDrawsAuthoredSlotAndPartOverrides)
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
            std::array<uint8_t, 16 * 16 * 4> pixels {};
            glBindTexture(GL_TEXTURE_2D, renderer.GetOutput(0));
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            std::array<uint8_t, 4> pixel {};
            std::copy_n(pixels.data() + (8 * 16 + 8) * 4, 4, pixel.data());
            return pixel;
        };
        EXPECT_EQ(draw(), (std::array<uint8_t, 4> {255, 0, 0, 255}));
        component.SetOverride(slot->EmbeddedMaterial, materials.at("slot"));
        EXPECT_EQ(draw(), (std::array<uint8_t, 4> {0, 255, 0, 255}));
        component.SetOverride(meshID, materials.at("part"));
        EXPECT_EQ(draw(), (std::array<uint8_t, 4> {0, 0, 255, 255}));
        EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
    }

    class MaterialSceneGL : public RenderAssetCacheGL
    {
    protected:
        void SetUp() override
        {
            RenderAssetCacheGL::SetUp();
            Assets     = CreateScope<AssetManager>(Provider, Registry({}));
            LogicScene = CreateRef<Scene>();
            Render     = CreateScope<RenderScene>(LogicScene);
            Render->OnViewportResize(16, 16);
            Renderer = CreateRef<SceneRenderer>(Render.get());
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
        std::array<uint8_t, 4> Pixel(int x = 8, int y = 8)
        {
            std::array<uint8_t, 16 * 16 * 4> image {};
            glBindTexture(GL_TEXTURE_2D, Renderer->GetOutput(0));
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.data());
            std::array<uint8_t, 4> pixel;
            std::copy_n(image.data() + (y * 16 + x) * 4, 4, pixel.data());
            return pixel;
        }
        float Depth(int x = 8)
        {
            std::array<float, 16 * 16> depth {};
            glBindTexture(GL_TEXTURE_2D, Renderer->GetOutput(2));
            glGetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_FLOAT, depth.data());
            return depth[8 * 16 + x];
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

    TEST_F(MaterialSceneGL, HighlightUsesReimportedMeshAfterQueuedGpuResourceIsDestroyed)
    {
        const auto material = Color({0, 1, 0, 1});
        const auto model    = Quad(material, 0, false, -1, 0);
        Ready();
        auto&                            cache    = *Renderer->GetAssetCache();
        const auto                       meshID   = Meshes.back();
        const std::weak_ptr<VertexArray> oldArray = cache.GetMesh(meshID)->VertexArray;

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
        EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));

        // Highlights are consumed by EndScene, rather than leaking into later frames.
        Renderer->BeginScene(Info);
        Renderer->EndScene();
        EXPECT_EQ(Pixel(6), (std::array<uint8_t, 4> {5, 5, 5, 255}));
        EXPECT_EQ(Pixel(12), (std::array<uint8_t, 4> {5, 5, 5, 255}));
    }

    TEST_F(MaterialSceneGL, TargetAlphaIsOpaqueEvenWhenProjectShadersReturnFractionalAlpha)
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
        glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
        for (const auto& model : {blend, opaque, mask})
        {
            Renderer->BeginScene(Info);
            Renderer->SubmitModel(model, glm::mat4(1));
            Renderer->EndScene();
            EXPECT_EQ(Pixel()[3], 255);
            GLboolean mask[4];
            glGetBooleanv(GL_COLOR_WRITEMASK, mask);
            EXPECT_EQ(mask[0], GL_FALSE);
            EXPECT_EQ(mask[1], GL_TRUE);
            EXPECT_EQ(mask[2], GL_FALSE);
            EXPECT_EQ(mask[3], GL_TRUE);
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
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
    }

    TEST_F(MaterialSceneGL, LegacyStencilOptionDrawsARingOutsideTheModelAndClearsPreviousReferences)
    {
        // Deferred submissions must preserve write-reference/exclude-reference intent, not the last GL state.
        auto library = CreateRef<ShaderLibrary>();
        library->Load(std::string(KANS_TEST_SHADER_DIR) + "/StencilShader.glsl");
        SceneRendererSpecification spec;
        spec.GeometryShaders = library;
        Renderer             = CreateRef<SceneRenderer>(Render.get(), spec);
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
        FrameBufferSpecification externalSpec;
        externalSpec.Width = externalSpec.Height = 16;
        externalSpec.SwapChainTarget             = true;
        externalSpec.AttachmentSpecification     = {FrameBufferTextureFormat::RGBA8, FrameBufferTextureFormat::Depth};
        auto external                            = FrameBuffer::Create(externalSpec);
        external->Bind();
        glEnable(GL_STENCIL_TEST);
        glStencilFuncSeparate(GL_FRONT, GL_EQUAL, 3, 0x37);
        glStencilFuncSeparate(GL_BACK, GL_GREATER, 4, 0x73);
        glStencilMaskSeparate(GL_FRONT, 0x12);
        glStencilMaskSeparate(GL_BACK, 0x21);
        glStencilOpSeparate(GL_FRONT, GL_INCR, GL_DECR, GL_INVERT);
        glStencilOpSeparate(GL_BACK, GL_ZERO, GL_REPLACE, GL_KEEP);
        glClearStencil(9);
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
            external->Bind();
        }
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
        external->Unbind();
        EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
    }

    TEST_F(MaterialSceneGL, LegacyOutlineCullsFrontFacesAndKeepsTheInflatedBackShell)
    {
        // Disabling culling paints the inflated front surface black outside its material silhouette.
        auto library = CreateRef<ShaderLibrary>();
        library->Load(std::string(KANS_TEST_SHADER_DIR) + "/OutLineShader.glsl");
        SceneRendererSpecification spec;
        spec.GeometryShaders = library;
        Renderer             = CreateRef<SceneRenderer>(Render.get(), spec);
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
        EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
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
            if (target == GL_UNIFORM_BUFFER && offset == 0)
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

    TEST_F(MaterialSceneGL, DeferredSubmissionDoesNotQueryDriverRasterState)
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
        EXPECT_EQ(Renderer->GetStats().DrawCalls, 2u);
    }

    TEST_F(MaterialSceneGL, FrameUniformsUploadOnlyAtDebugViewTransitionsIncludingLitPreview)
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
        EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
    }

    TEST_F(MaterialSceneGL, BlendSortsAcrossModelsAndPreviewByTransformedBoundsAfterOpaque)
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
        EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
    }

    TEST_F(MaterialSceneGL, BlendTestsOpaqueDepthAndLeavesUncoveredDepthClear)
    {
        // A blend behind the opaque half must be occluded, and must not write depth in the other half.
        const auto opaque = Quad(Color({0, 1, 0, 1}), 0, false, 0, 1);
        const auto blend  = Quad(Color({1, 0, 0, 0.5}, MaterialAlphaMode::Blend), 0.5f);
        Ready();
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

    TEST_F(MaterialSceneGL, BlendDistanceUsesTheCameraAndEachPartsBoundsWithinOneModel)
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

    TEST_F(MaterialSceneGL, StandardMaskMultipliesTextureFactorAndVertexAlphaAndRetainsCutoffEquality)
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
        cache.GetTexture(textureID)->Bind(12);
        glActiveTexture(GL_TEXTURE12);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
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

    TEST_F(MaterialSceneGL, FailedCustomShaderAndDebugFallbackKeepBlendAndDoubleSidedState)
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

    TEST_F(MaterialSceneGL, DoubleSidedBackFacesFlipNormalsAndDoNotDisableCullingForTheNextPart)
    {
        MaterialSource source;
        source.Parameters["BaseColor"] = {1, 0, 0, 1};
        source.Render.DoubleSided      = true;
        const auto doubleSided         = Quad(Material(source, false), 0, true, -1, 0);
        source.Render.DoubleSided      = false;
        const auto singleSided         = Quad(Material(source, false), 0, true, 0, 1);
        Ready();
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
        EXPECT_EQ(glIsEnabled(GL_CULL_FACE), GL_FALSE);
    }

    TEST_F(MaterialSceneGL, RenderStateRestoresDepthCullBlendFunctionsAndWireframeModeAfterPreviewOutline)
    {
        // A viewport render must not change the raster state used by the following UI/geometry pass.
        const auto model = Quad(Color({1, 0, 0, 0.5}, MaterialAlphaMode::Blend, true), 0, false, -0.5f, 0.5f);
        Ready();
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
        glDisable(GL_DEPTH_TEST);
        glDepthFunc(GL_GREATER);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);
        glBlendEquationSeparate(GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        Renderer->BeginScene(Info);
        Renderer->SetPreview(model, glm::mat4(1));
        Renderer->EndScene();
        EXPECT_EQ(glIsEnabled(GL_CULL_FACE), GL_TRUE);
        EXPECT_EQ(glIsEnabled(GL_DEPTH_TEST), GL_FALSE);
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
        EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
    }

    class SurfacePickerGL : public RenderAssetCacheGL, public testing::WithParamInterface<bool>
    {};

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
        const bool      perspective    = GetParam();
        const glm::mat4 projection     = perspective ? glm::perspective(glm::radians(90.0f), 2.0f, 1.0f, 9.0f) :
                                                       glm::ortho(-8.0f, 8.0f, -4.0f, 4.0f, 1.0f, 9.0f);
        const glm::mat4 viewProjection = projection * glm::translate(glm::mat4(1.0f), {-10, -20, -30});
        const MeshDraw  near {&mesh, glm::translate(glm::mat4(1.0f), {10, 20, 26}), 7};
        const MeshDraw  far {&mesh, glm::translate(glm::mat4(1.0f), {10, 20, 24}), 9};

        // Picking must establish its depth state even after other render passes.
        glDepthMask(GL_FALSE);
        glDepthFunc(GL_GREATER);
        glDepthRange(0.25, 0.75);
        glClearDepth(0.0);
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
