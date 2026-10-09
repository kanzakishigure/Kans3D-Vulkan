#include <glad/glad.h>
#include <gtest/gtest.h>
#ifdef KANS_TEST_USE_EGL
#include <EGL/egl.h>
#include <EGL/eglext.h>
#else
#include <GLFW/glfw3.h>
#endif
#include <chrono>
#include <fstream>
#include <sstream>
#include <map>
#include <thread>
#include "Kans3D/Asset/Product/ContentDigest.h"
#include "Kans3D/Asset/Product/MeshProduct.h"
#include "Kans3D/Asset/Product/ModelProduct.h"
#include "Kans3D/Asset/Product/ShaderProduct.h"
#include "Kans3D/Asset/Product/TextureProduct.h"
#include "Kans3D/Asset/Importer/MaterialSource.h"
#include "Kans3D/Core/Log/Log.h"
#include "Kans3D/Platform/OpenGL/OpenGLRHI.h"
#include "Kans3D/Platform/OpenGL/OpenGLShader.h"
#include "Kans3D/Renderer/RHI/RendererAPI.h"
#include "Kans3D/Renderer/Resource/RenderAssetCache.h"
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

    // Renders a quad with the engine Product PBR shader into an offscreen target.
    TEST_F(RenderAssetCacheGL, ProductPbrShaderShadesMaterialFactorsWithSceneLights)
    {
        OpenGLShader shader(std::string(KANS_TEST_SHADER_DIR) + "/PBR/ProductPBR.glsl");
        shader.Bind();
        GLint program = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        ASSERT_NE(program, 0);

        constexpr int Size        = 16;
        GLuint        framebuffer = 0, color = 0, depth = 0;
        glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glGenTextures(1, &color);
        glBindTexture(GL_TEXTURE_2D, color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, Size, Size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        glGenRenderbuffers(1, &depth);
        glBindRenderbuffer(GL_RENDERBUFFER, depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, Size, Size);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
        ASSERT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GLenum(GL_FRAMEBUFFER_COMPLETE));
        glViewport(0, 0, Size, Size);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // A quad covering the left half of clip space, facing +Z.
        MeshData quad;
        for (auto [x, y] : {std::pair {-1.0f, -1.0f}, {0.0f, -1.0f}, {0.0f, 1.0f}, {-1.0f, 1.0f}})
        {
            MeshVertexData vertex;
            vertex.Position = {x, y, 0};
            vertex.Normal   = {0, 0, 1};
            quad.Vertices.push_back(vertex);
        }
        quad.Indices  = {0, 1, 2, 2, 3, 0};
        auto uploaded = UploadMesh(quad);
        ASSERT_TRUE(std::holds_alternative<GpuMesh>(uploaded));
        TextureData white;
        white.Width = white.Height = 1;
        white.Mips                 = {{{std::byte {255}, std::byte {255}, std::byte {255}, std::byte {255}}}};
        auto texture               = Texture2D::CreateFromProduct(white);
        ASSERT_TRUE(std::holds_alternative<Ref<Texture2D>>(texture));
        for (uint32_t slot = 0; slot < 5; ++slot)
            std::get<Ref<Texture2D>>(texture)->Bind(slot);

        shader.UploadUniformMat4("U_ViewProjection", glm::mat4(1.0f));
        shader.UploadUniformMat4("U_Transform", glm::mat4(1.0f));
        shader.UploadUniform3Float("U_ViewPos", {0, 0, 2});
        for (int i = 0; i < 4; ++i)
        {
            shader.UploadUniform3Float("lightPositions[" + std::to_string(i) + "]", {0, 0, 1});
            shader.UploadUniform3Float("lightColors[" + std::to_string(i) + "]", {2, 2, 2});
        }
        shader.UploadUniform4Float("U_BaseColor", {1, 0, 0, 1});
        shader.UploadUniformFloat("U_Metallic", 0);
        shader.UploadUniformFloat("U_Roughness", 1);
        const char* samplers[] = {"U_AlbedoTexture",
                                  "U_NormalTexture",
                                  "U_MetallicRoughnessTexture",
                                  "U_OcclusionTexture",
                                  "U_EmissiveTexture"};
        for (int i = 0; i < 5; ++i)
            shader.UploadUniformInt(samplers[i], i);
        // As SceneRenderer does: environment samplers on their own units. Cube and 2D
        // samplers sharing a unit make draws fail with GL_INVALID_OPERATION.
        shader.UploadUniformInt("U_IrradianceMap", 5);
        shader.UploadUniformInt("U_PrefilterMap", 6);
        shader.UploadUniformInt("U_BrdfLUTMap", 7);
        shader.UploadUniformInt("U_HasNormalTexture", 0);
        shader.UploadUniformInt("U_HasEmissiveTexture", 0);
        const auto& mesh = std::get<GpuMesh>(uploaded);
        mesh.VertexArray->Bind();
        glDrawElements(GL_TRIANGLES, GLsizei(mesh.IndexCount), GL_UNSIGNED_INT, nullptr);
        EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));

        std::array<uint8_t, Size * Size * 4> pixels {};
        glReadPixels(0, 0, Size, Size, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        const auto pixel = [&](int x, int y) { return &pixels[(y * Size + x) * 4]; };
        // Lit red where the quad is, untouched black elsewhere.
        const uint8_t* lit = pixel(Size / 4, Size / 2);
        EXPECT_GT(lit[0], 60);
        EXPECT_LT(lit[1], lit[0] / 4);
        EXPECT_LT(lit[2], lit[0] / 4);
        const uint8_t* empty = pixel(3 * Size / 4, Size / 2);
        EXPECT_EQ(empty[0] + empty[1] + empty[2], 0);

        // The back side of the light: same quad, light behind it, is darker.
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        for (int i = 0; i < 4; ++i)
            shader.UploadUniform3Float("lightPositions[" + std::to_string(i) + "]", {0, 0, -1});
        glDrawElements(GL_TRIANGLES, GLsizei(mesh.IndexCount), GL_UNSIGNED_INT, nullptr);
        std::array<uint8_t, Size * Size * 4> unlit {};
        glReadPixels(0, 0, Size, Size, GL_RGBA, GL_UNSIGNED_BYTE, unlit.data());
        EXPECT_LT(unlit[(Size / 2 * Size + Size / 4) * 4], lit[0]);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteRenderbuffers(1, &depth);
        glDeleteTextures(1, &color);
        glDeleteFramebuffers(1, &framebuffer);
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
        TextureData whiteImage;
        whiteImage.Width = whiteImage.Height = 1;
        whiteImage.Mips = {{{std::byte {255}, std::byte {255}, std::byte {255}, std::byte {255}}}};
        auto white      = std::get<Ref<Texture2D>>(Texture2D::CreateFromProduct(whiteImage));

        const auto draw = [&](AssetID materialID) -> std::array<uint8_t, 4> {
            const auto* bindings = cache.GetMaterialBindings(materialID);
            program->Bind();
            for (const auto& [binding, buffer] : bindings->Blocks)
                buffer.Bind(program->MaterialBlockPoints().at(binding));
            for (const auto& [binding, unit] : program->MaterialSamplerUnits())
            {
                Ref<Texture2D> texture = white;
                for (const auto& [textureBinding, id] : bindings->Textures)
                    if (textureBinding == binding)
                        texture = cache.GetTexture(id);
                texture->Bind(unit);
            }
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
} // namespace
