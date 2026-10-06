#include <glad/glad.h>
#include <gtest/gtest.h>
#ifdef KANS_TEST_USE_EGL
#include <EGL/egl.h>
#include <EGL/eglext.h>
#else
#include <GLFW/glfw3.h>
#endif
#include <chrono>
#include <map>
#include <thread>
#include "Kans3D/Asset/Product/ContentDigest.h"
#include "Kans3D/Asset/Product/MeshProduct.h"
#include "Kans3D/Asset/Product/ModelProduct.h"
#include "Kans3D/Asset/Product/TextureProduct.h"
#include "Kans3D/Core/Log/Log.h"
#include "Kans3D/Platform/OpenGL/OpenGLRHI.h"
#include "Kans3D/Renderer/RHI/RendererAPI.h"
#include "Kans3D/Renderer/Resource/RenderAssetCache.h"

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
} // namespace
