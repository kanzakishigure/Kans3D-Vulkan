#include <glad/glad.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <thread>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>
#include "Kans3D/Asset/Importer/MaterialSource.h"
#include "Kans3D/Asset/Importer/ShaderImporter.h"
#include "Kans3D/Asset/Product/ContentDigest.h"
#include "Kans3D/Asset/Product/MeshProduct.h"
#include "Kans3D/Asset/Product/ModelProduct.h"
#include "Kans3D/Asset/Product/ShaderProduct.h"
#include "Kans3D/Asset/Product/TextureProduct.h"
#include "Kans3D/Core/Log/Log.h"
#include "Kans3D/FileSystem/FileSystem.h"
#include "Kans3D/RHI/Backend.h"
#include "Kans3D/Renderer/MaterialPreviewRenderer.h"
#include "Kans3D/Renderer/Renderer2D.h"
#include "Kans3D/Renderer/SceneRenderer.h"
#include "Kans3D/Scene/Entity.h"
#include "GpuTestContext.h"
#include "ReferenceImage.h"

namespace
{
    using namespace Kans;
    using namespace Kans::Test;
    namespace fs = std::filesystem;

    template<typename T>
    T Value(std::variant<T, AssetError> value)
    {
        if (const auto* error = std::get_if<AssetError>(&value))
            throw std::runtime_error(error->Message);
        return std::get<T>(std::move(value));
    }
    class ReferenceProvider final : public IAssetProvider
    {
    public:
        std::map<std::string, std::vector<std::byte>>    Files;
        std::variant<std::vector<std::byte>, AssetError> Read(const ProductRecord& record) const override
        {
            return Files.at(record.Location.RelativeFile.ToString());
        }
    };

    // All tests in this file share one context/environment when run together.
    // CTest may run each case in its own process; each still owns its setup.
    class RendererReferenceGL : public testing::Test
    {
    protected:
        static constexpr uint32_t                     Size = 128;
        inline static std::unique_ptr<GpuTestContext> Context;
        inline static fs::path                        Directory;
        inline static Ref<ReferenceProvider>          Provider;
        inline static SourceAssetID                   Source;
        inline static uint64_t                        NextID = 0;
        inline static std::vector<ProductRecord>      Records;
        inline static Scope<AssetManager>             Assets;
        inline static Ref<Scene>                      Logic;
        inline static Scope<RenderScene>              Render;
        inline static Scope<SceneRenderer>            Renderer;
        inline static RenderHardware::Backend         PreviousBackend;
        inline static SceneInfo                       Info {};
        inline static AssetID                         Albedo, Normal, Textured, Emissive, Toon, ToonShader;
        inline static ProductModelComponent           Sphere, Quad, EmissionSphere, ToonSphere;
        inline static std::vector<AssetID>            Models, Meshes, Materials, Textures;
        inline static bool                            SpriteInitialized = false;

        static AssetID Add(AssetType type, std::vector<std::byte> bytes)
        {
            const AssetID id {Source, ++NextID};
            ProductRecord record;
            record.Metadata.assetID       = id;
            record.Metadata.Type          = type;
            record.Location.RelativeFile  = AssetPath("reference-" + std::to_string(NextID));
            record.Location.ByteSize      = bytes.size();
            record.Location.ContentDigest = ComputeContentDigest(bytes);
            Provider->Files.emplace(record.Location.RelativeFile.ToString(), std::move(bytes));
            Records.push_back(record);
            return id;
        }
        static AssetID Material(MaterialSource source, const ShaderData* shader = nullptr)
        {
            const auto id =
                Add(AssetType::Material, Value(EncodeMaterialProduct(Value(ResolveMaterialSource(source, shader)))));
            Materials.push_back(id);
            return id;
        }
        static AssetID Color(glm::vec4 color, MaterialAlphaMode mode = MaterialAlphaMode::Opaque, bool back = false)
        {
            MaterialSource source;
            source.Parameters["BaseColor"] = {color.r, color.g, color.b, color.a};
            source.Parameters["Roughness"] = {0.6};
            source.Render.AlphaMode        = mode;
            source.Render.DoubleSided      = back;
            return Material(source);
        }
        static TextureData Pattern(bool normal)
        {
            TextureData data;
            data.Width = data.Height = 16;
            data.ColorSpace          = normal ? TextureColorSpace::Linear : TextureColorSpace::SRGB;
            data.Usage               = normal ? TextureUsage::Normal : TextureUsage::Color;
            data.Origin              = TextureOrigin::TopLeft;
            data.Mips.resize(1);
            for (unsigned y = 0; y < 16; ++y)
                for (unsigned x = 0; x < 16; ++x)
                {
                    // Unequal quadrants + off-centre mark distinguish Y orientation,
                    // sRGB decode, alpha cutoff and tangent-space normal direction.
                    std::array<uint8_t, 4> pixel;
                    if (normal)
                        pixel = {uint8_t(x < 5 ? 190 : 100), uint8_t(y < 9 ? 160 : 80), 235, 255};
                    else
                        pixel = {uint8_t(y < 6 ? 220 : 45),
                                 uint8_t(x < 10 ? 90 : 205),
                                 uint8_t((x < 5 && y > 9) ? 240 : 55),
                                 uint8_t((x + 2 * y) % 9 < 4 ? 64 : 230)};
                    for (auto channel : pixel)
                        data.Mips[0].Pixels.push_back(std::byte(channel));
                }
            return Value(GenerateTextureMips(data));
        }
        static AssetID Texture(TextureData data)
        {
            const auto id = Add(AssetType::Texture, Value(EncodeTextureProduct(data)));
            Textures.push_back(id);
            return id;
        }
        static MeshData QuadData(bool back = false)
        {
            MeshData mesh;
            for (const auto& xy : {glm::vec2 {-1, -1}, {1, -1}, {1, 1}, {-1, 1}})
            {
                MeshVertexData v;
                v.Position = {xy.x, xy.y, 0};
                v.Normal   = {0, 0, 1};
                v.UV       = {(xy.x + 1) / 2, (xy.y + 1) / 2};
                v.Tangent  = {1, 0, 0, 1};
                mesh.Vertices.push_back(v);
            }
            mesh.Indices = back ? std::vector<uint32_t> {2, 1, 0, 0, 3, 2} : std::vector<uint32_t> {0, 1, 2, 2, 3, 0};
            return mesh;
        }
        static MeshData SphereData()
        {
            // UV sphere with explicit tangent handedness, built as a Mesh Product
            // and uploaded by RenderAssetCache, never a test-only GL draw.
            constexpr unsigned longitude = 32, latitude = 16;
            constexpr float    pi = 3.14159265358979323846f;
            MeshData           mesh;
            for (unsigned y = 0; y <= latitude; ++y)
                for (unsigned x = 0; x <= longitude; ++x)
                {
                    const float    theta = float(y) / latitude * pi, phi = float(x) / longitude * 2 * pi;
                    MeshVertexData v;
                    v.Position =
                        v.Normal = {std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
                    v.UV         = {float(x) / longitude, 1 - float(y) / latitude};
                    v.Tangent    = {-std::sin(phi), 0, std::cos(phi), -1};
                    mesh.Vertices.push_back(v);
                }
            for (unsigned y = 0; y < latitude; ++y)
                for (unsigned x = 0; x < longitude; ++x)
                {
                    const unsigned a = y * (longitude + 1) + x, b = a + longitude + 1;
                    mesh.Indices.insert(mesh.Indices.end(), {a, a + 1, b, a + 1, b + 1, b});
                }
            return mesh;
        }
        static ProductModelComponent Model(MeshData mesh, AssetID material)
        {
            mesh.Material = material;
            const auto id = Add(AssetType::StaticMesh, Value(EncodeMeshProduct(mesh)));
            Meshes.push_back(id);
            ModelData model;
            model.Nodes.push_back({"Root", -1, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, {id}});
            ProductModelComponent component;
            component.Model = Add(AssetType::Model, Value(EncodeModelProduct(model)));
            Models.push_back(component.Model);
            return component;
        }
        static glm::mat4 Transform(glm::vec3 position, glm::vec3 scale, float angle = 0)
        {
            return glm::translate(glm::mat4(1), position) * glm::rotate(glm::mat4(1), angle, {0, 0, 1}) *
                   glm::scale(glm::mat4(1), scale);
        }
        static void Ready()
        {
            auto registry = Value(AssetRegistry::FromProducts(Records));
            Value(Assets->SetRegistry(CreateRef<const AssetRegistry>(std::move(registry))));
            auto&      cache    = *Renderer->GetAssetCache();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            bool       ready    = false;
            do
            {
                cache.BeginFrame();
                ready = true;
                for (auto id : Models)
                    ready &= cache.GetModel(id) != nullptr;
                for (auto id : Meshes)
                    ready &= cache.GetMesh(id) != nullptr;
                for (auto id : Materials)
                    ready &= cache.GetMaterialBindings(id) != nullptr;
                for (auto id : Textures)
                    ready &= cache.GetTexture(id) != nullptr;
                ready &= cache.GetShader(ToonShader) != nullptr;
                if (!ready)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } while (!ready && std::chrono::steady_clock::now() < deadline);
            ASSERT_TRUE(ready) << "Real asset/cache resources did not become ready";
            ASSERT_EQ(cache.GetMaterialBindings(Toon)->Shader, ToonShader) << "Toon must not fall back to Standard";
        }
        static void WriteEnvironment()
        {
            // Raw Radiance RGBE, 8x4; unequal bright lobes test cubemap orientation
            // and roughness mip sampling. Small precompute targets keep Mesa fast.
            std::ofstream out(Directory / "environment.hdr", std::ios::binary);
            out << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 4 +X 8\n";
            for (unsigned y = 0; y < 4; ++y)
                for (unsigned x = 0; x < 8; ++x)
                {
                    const std::array<uint8_t, 4> rgbe {
                        uint8_t(45 + x * 20), uint8_t(40 + y * 35), uint8_t(x < 3 ? 150 : 65), 129};
                    out.write(reinterpret_cast<const char*>(rgbe.data()), rgbe.size());
                }
            ASSERT_TRUE(out.good());
        }
        static void SetUpTestSuite()
        {
            if (!Log::GetCoreLogger())
                Log::Init();
            Context = std::make_unique<GpuTestContext>(GpuTestBackend::OpenGL);
            ASSERT_NO_FATAL_FAILURE(Context->SetUp());
            PreviousBackend = RenderHardware::GetBackend();
            RenderHardware::SetBackend(RenderHardware::Backend::OpenGL);
            Directory = fs::temp_directory_path() / ("kans-render-reference-" + SourceAssetID::Generate().ToString());
            fs::create_directories(Directory / "stage");
            Provider = CreateRef<ReferenceProvider>();
            Source   = SourceAssetID::Generate();
            Assets = CreateScope<AssetManager>(Provider,
                                               CreateRef<const AssetRegistry>(Value(AssetRegistry::FromProducts({}))));
            Logic  = CreateRef<Scene>();
            Render = CreateScope<RenderScene>(Logic);
            Render->OnViewportResize(Size, Size);
            SceneRendererSpecification spec;
            Renderer              = CreateScope<SceneRenderer>(Render.get(), spec);
            Renderer->SetAssetManager(Assets.get());
            Info.sceneCamera.Position   = {0, 0, 4};
            Info.sceneCamera.viewMatrix = glm::lookAt(Info.sceneCamera.Position, glm::vec3(0), glm::vec3(0, 1, 0));
            Info.sceneCamera.camera     = Camera(glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 20.0f));
            Info.pointLight.Position    = {-2, 3, 4};
            Info.pointLight.Ambient_Intensity  = {30, 24, 20};
            Info.pointLight.Diffuse_Intensity  = {1, 1, 1};
            Info.pointLight.Specular_Intensity = {1, 1, 1};
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            glDisable(GL_CULL_FACE);
            glDisable(GL_FRAMEBUFFER_SRGB); // Material shaders explicitly encode the LDR output.
            ASSERT_NO_FATAL_FAILURE(WriteEnvironment());
            Render->PrepareEnvironment(Directory / "environment.hdr");
            Renderer->PrepareEnvironment({32, 8, 32, 32}); // Same production GLSL and integration sample counts.
            ASSERT_TRUE(Renderer->GetEnvironment().IrradianceMap);
            ASSERT_TRUE(Renderer->GetEnvironment().RadianceMap);
            ASSERT_TRUE(Renderer->GetBrdfLUT());
            Albedo = Texture(Pattern(false));
            Normal = Texture(Pattern(true));
            MaterialSource standard;
            standard.Parameters = {{"BaseColor", {0.9, 0.75, 0.6, 1}},
                                   {"Metallic", {0.35}},
                                   {"Roughness", {0.42}},
                                   {"UVTiling", {2.3, 1.7}},
                                   {"UVOffset", {0.13, 0.07}}};
            standard.Textures   = {{"BaseColorMap", Albedo}, {"NormalMap", Normal}};
            Textured            = Material(standard);
            Sphere              = Model(SphereData(), Textured);
            Quad                = Model(QuadData(), Textured);
            MaterialSource emission;
            emission.Parameters              = {{"BaseColor", {0.02, 0.03, 0.04, 1}},
                                                {"EmissiveColor", {0.9, 0.12, 0.5}},
                                                {"EmissiveIntensity", {3.5}}};
            emission.Textures["EmissiveMap"] = Albedo;
            Emissive                         = Material(emission);
            EmissionSphere                   = Model(SphereData(), Emissive);
            // Import the actual editor sample, including its shaderprog defaults.
            ImportContext context;
            context.SourceID         = SourceAssetID::Generate();
            context.AssetRoot        = fs::path(KANS_TEST_EDITOR_ASSET_DIR);
            context.SourceFile       = context.AssetRoot / "shaders/Toon/Toon.shaderprog";
            context.StagingDirectory = Directory / "stage";
            context.Target           = "vulkan12";
            const auto inputs        = Value(ShaderImporter {}.DiscoverInputs(context));
            for (const auto& file : inputs.SourceFiles)
                context.SourceInputs.emplace(file, SourceAssetID::Generate());
            const auto imported = Value(ShaderImporter {}.Import(context));
            ASSERT_EQ(imported.Products.size(), 1u);
            auto bytes  = Value(ReadProductFile(context.StagingDirectory / imported.Products[0].StagedFile));
            auto shader = Value(DecodeShaderProduct(bytes.data(), bytes.size()));
            ToonShader  = Add(AssetType::Shader, std::move(bytes));
            MaterialSource toon;
            toon.Shader               = ToonShader;
            toon.Parameters           = {{"BaseColor", {0.3, 0.8, 0.65, 1}},
                                         {"ShadowColor", {0.2, 0.09, 0.4, 1}},
                                         {"ShadowThreshold", {0.75}},
                                         {"RimColor", {1, 0.2, 0.08, 0.7}}};
            toon.Textures["u_Albedo"] = Albedo;
            Toon                      = Material(toon, &shader);
            ToonSphere                = Model(SphereData(), Toon);
            ASSERT_NO_FATAL_FAILURE(Ready());
            EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
        }
        static void TearDownTestSuite()
        {
            Renderer.reset();
            Render.reset();
            Logic.reset();
            Assets.reset();
            Provider.reset();
            if (SpriteInitialized)
            {
                Renderer2D::Shutdown();
                KansFileSystem::ShutDown();
            }
            if (!Directory.empty())
                fs::remove_all(Directory);
            RenderHardware::SetBackend(PreviousBackend);
            Context.reset();
        }
        void SetUp() override
        {
            ASSERT_TRUE(Renderer);
            RecordProperty("renderer", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
            Renderer->SetViewMode(SceneViewMode::Lit);
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_FRAMEBUFFER_SRGB);
            glDisable(GL_STENCIL_TEST);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            glFrontFace(GL_CCW);
            glDisable(GL_BLEND);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glViewport(0, 0, Size, Size);
        }
        static RgbaImage Read(uint32_t texture)
        {
            RgbaImage image {Size, Size, std::vector<uint8_t>(Size * Size * 4)};
            glBindTexture(GL_TEXTURE_2D, texture);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.Pixels.data());
            for (unsigned y = 0; y < Size / 2; ++y)
                std::swap_ranges(image.Pixels.begin() + y * Size * 4,
                                 image.Pixels.begin() + (y + 1) * Size * 4,
                                 image.Pixels.begin() + (Size - 1 - y) * Size * 4);
            return image;
        }
        static void Compare(const char* name, const RgbaImage& image, ImageTolerance tolerance)
        {
            const auto result = CompareReference(image,
                                                 fs::path(KANS_TEST_REFERENCE_DIR) / (std::string(name) + ".png"),
                                                 KANS_TEST_OUTPUT_DIR,
                                                 tolerance);
            EXPECT_TRUE(result.Passed) << result.Message;
            EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
        }
        static void CompareScene(const char* name, ImageTolerance tolerance)
        {
            Compare(name, Read(Renderer->GetOutput(0)), tolerance);
        }
        static void TexturedScene()
        {
            Renderer->BeginScene(Info);
            Renderer->SubmitModel(Sphere, Transform({-0.38f, 0.22f, 0.15f}, {0.72f, 0.72f, 0.72f}, 0.23f));
            Renderer->SubmitModel(Quad, Transform({0.57f, -0.46f, -0.35f}, {0.48f, 0.35f, 1}, -0.18f));
            Renderer->EndScene();
            ASSERT_EQ(Renderer->GetStats().DrawCalls, 2u);
        }
    };

    // Lit images allow 2/255 for half-float IBL/filtering and LDR quantization.
    // NO outlier pixels are allowed: edges, orientation and coverage remain strict.
    // Unlit geometry/debug images need only 1/255 rounding tolerance. These are
    // fixed explicit budgets, not inferred or increased after reference comparison.
    TEST_F(RendererReferenceGL, StandardTexturedIblPointLight)
    {
        ASSERT_NO_FATAL_FAILURE(TexturedScene());
        CompareScene("standard-ibl-point", {2, 0.0});
    }
    TEST_F(RendererReferenceGL, AlphaModesDepthSortingAndBackFaces)
    {
        const auto     opaque = Model(QuadData(), Color({0.12f, 0.32f, 0.8f, 1}));
        MaterialSource mask;
        mask.Render.AlphaMode         = MaterialAlphaMode::Mask;
        mask.Render.AlphaCutoff       = 0.5f;
        mask.Parameters["BaseColor"]  = {1, 0.85, 0.2, 1};
        mask.Textures["BaseColorMap"] = Albedo;
        const auto masked             = Model(QuadData(), Material(mask));
        const auto near   = Model(QuadData(), Color({0.95f, 0.08f, 0.15f, 0.45f}, MaterialAlphaMode::Blend));
        const auto far    = Model(QuadData(), Color({0.05f, 0.9f, 0.2f, 0.65f}, MaterialAlphaMode::Blend));
        const auto back   = Model(QuadData(true), Color({0.8f, 0.3f, 0.9f, 1}, MaterialAlphaMode::Opaque, true));
        const auto culled = Model(QuadData(true), Color({1, 1, 1, 1}));
        ASSERT_NO_FATAL_FAILURE(Ready());
        Renderer->BeginScene(Info);
        // Intentionally submit near before far and opaque last. Renderer must
        // sort across models, test opaque depth, and avoid writing blend depth.
        Renderer->SubmitModel(near, Transform({0.1f, -0.15f, 0.5f}, {0.62f, 0.56f, 1}));
        Renderer->SubmitModel(far, Transform({0.37f, 0.03f, 0.1f}, {0.67f, 0.6f, 1}));
        Renderer->SubmitModel(masked, Transform({-0.58f, 0.4f, 0.65f}, {0.6f, 0.5f, 1}));
        Renderer->SubmitModel(back, Transform({-0.83f, -0.75f, 0.3f}, {0.32f, 0.22f, 1}));
        Renderer->SubmitModel(culled, Transform({0.9f, 0.92f, 0.2f}, {0.28f, 0.18f, 1}));
        Renderer->SubmitModel(opaque, Transform({-0.1f, 0.1f, 0.35f}, {0.4f, 0.28f, 1}));
        Renderer->EndScene();
        EXPECT_EQ(Renderer->GetStats().DrawCalls, 6u);
        CompareScene("alpha-depth-sort-backfaces", {2, 0.0});
    }
    TEST_F(RendererReferenceGL, EmissionTextureAndToneMapping)
    {
        Renderer->BeginScene(Info);
        Renderer->SubmitModel(EmissionSphere, Transform({0.25f, 0.28f, 0}, {0.8f, 0.8f, 0.8f}, -0.3f));
        Renderer->EndScene();
        CompareScene("emission", {2, 0.0});
    }
    TEST_F(RendererReferenceGL, ImportedToonMaterial)
    {
        Renderer->BeginScene(Info);
        Renderer->SubmitModel(ToonSphere, Transform({-0.22f, 0.13f, 0}, {0.93f, 0.93f, 0.93f}, 0.3f));
        Renderer->EndScene();
        CompareScene("toon-imported", {1, 0.0});
    }
    TEST_F(RendererReferenceGL, DebugBaseColor)
    {
        Renderer->SetViewMode(SceneViewMode::BaseColor);
        ASSERT_NO_FATAL_FAILURE(TexturedScene());
        CompareScene("debug-base-color", {1, 0.0});
    }
    TEST_F(RendererReferenceGL, DebugNormals)
    {
        Renderer->SetViewMode(SceneViewMode::Normals);
        ASSERT_NO_FATAL_FAILURE(TexturedScene());
        CompareScene("debug-normals", {1, 0.0});
    }
    TEST_F(RendererReferenceGL, DebugWireframe)
    {
        Renderer->SetViewMode(SceneViewMode::Wireframe);
        ASSERT_NO_FATAL_FAILURE(TexturedScene());
        // Raster coverage is the regression being tested, so no edge exclusions.
        CompareScene("debug-wireframe", {1, 0.0});
    }
    TEST_F(RendererReferenceGL, SelectionOutlineAndNumericSurfacePicker)
    {
        const auto transform = Transform({-0.3f, 0.23f, 0.2f}, {0.55f, 0.4f, 1}, 0.15f);
        Renderer->BeginScene(Info);
        Renderer->SubmitModel(Quad, transform);
        Renderer->SetHighlight({{Quad, transform}});
        Renderer->EndScene();
        CompareScene("selection-outline", {2, 0.0});
        std::vector<MeshDraw> draws;
        Renderer->AppendModelDraws(Quad, transform, 37, draws);
        Renderer->AppendModelDraws(Quad, Transform({-0.3f, 0.23f, -0.5f}, {0.7f, 0.6f, 1}), 91, draws);
        ASSERT_EQ(draws.size(), 2u);
        const auto       vp = Info.sceneCamera.camera.GetProjectionMatrix() * Info.sceneCamera.viewMatrix;
        const glm::uvec2 pixel {52, 73};
        EXPECT_EQ(Renderer->Pick(draws, vp, pixel), 37u);
        const auto hit = Renderer->PickSurface(draws, vp, pixel);
        ASSERT_EQ(hit.ID, 37u);
        ASSERT_TRUE(hit.Position);
        // Pixel centres unproject onto the known z=0.2 plane. This detects a
        // Vulkan depth-range mismatch as well as Y-origin/off-by-one errors.
        const float distance = 4.0f - 0.2f;
        const float extent   = distance * std::tan(glm::radians(45.0f) / 2);
        EXPECT_NEAR(hit.Position->x, ((pixel.x + 0.5f) / Size * 2 - 1) * extent, 0.0001f);
        EXPECT_NEAR(hit.Position->y, ((pixel.y + 0.5f) / Size * 2 - 1) * extent, 0.0001f);
        EXPECT_NEAR(hit.Position->z, 0.2f, 0.0001f);
        const auto far = Renderer->PickSurface(draws, vp, {77, 80});
        ASSERT_EQ(far.ID, 91u); // Exposed part of the larger, farther quad.
        ASSERT_TRUE(far.Position);
        EXPECT_NEAR(far.Position->z, -0.5f, 0.0001f);
        const auto empty = Renderer->PickSurface(draws, vp, {2, 2});
        EXPECT_EQ(empty.ID, 0u);
        EXPECT_FALSE(empty.Position);
        EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
    }
    TEST_F(RendererReferenceGL, MaterialPreviewSphere)
    {
        MaterialPreviewRenderer preview(*Renderer);
        MaterialPreviewOptions  options;
        options.Size         = {Size, Size};
        options.Rotation     = {0.37f, -0.23f};
        const auto thumbnail = preview.RenderThumbnail(Textured, options);
        ASSERT_TRUE(thumbnail);
        Compare("preview-sphere", {Size, Size, thumbnail->Pixels}, {2, 0.0});
    }
    TEST_F(RendererReferenceGL, Grid)
    {
        SceneInfo info              = Info;
        info.sceneCamera.Position   = {2, 2.2f, 3};
        info.sceneCamera.viewMatrix = glm::lookAt(info.sceneCamera.Position, glm::vec3(0), glm::vec3(0, 1, 0));
        Renderer->BeginScene(info);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        Renderer->SubmitGrid(0.035f, 12.0f, Transform({0.1f, -0.3f, 0}, {2.0f, 1, 2.0f}), {true});
        Renderer->EndScene();
        CompareScene("grid", {1, 0.0});
    }
    TEST_F(RendererReferenceGL, Skybox)
    {
        SceneInfo info = Info;
        info.sceneCamera.viewMatrix =
            glm::lookAt(info.sceneCamera.Position, glm::vec3(0.8f, 0.4f, 0), glm::vec3(0, 1, 0));
        Renderer->BeginScene(info);
        glDisable(GL_CULL_FACE);
        glDepthFunc(GL_LEQUAL);
        Renderer->SubmitSkybox(Renderer->GetSkybox());
        Renderer->EndScene();
        CompareScene("skybox", {2, 0.0});
    }
    TEST_F(RendererReferenceGL, Renderer2DSprite)
    {
        // Renderer2D locates its real TextureShader through the filesystem service.
        // Only a temp ini is written; editor resources remain read-only.
        if (!SpriteInitialized)
        {
            std::ofstream config(Directory / "filesystem.ini");
            config << "ShaderDir=" << fs::path(KANS_TEST_SHADER_DIR).parent_path().generic_string() << "/\n";
            config.close();
            ASSERT_TRUE(config.good());
            KansFileSystem::Init(Directory / "filesystem.ini");
            Renderer2D::Init();
            SpriteInitialized = true;
        }
        auto texture = Renderer->GetAssetCache()->GetTexture(Albedo);
        ASSERT_TRUE(texture);
        Renderer->BeginScene(Info);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        Renderer2D::ResetStats();
        Renderer2D::BeginScene(Info.sceneCamera.camera, Info.sceneCamera.viewMatrix, {true});
        Renderer2D::DrawQuad(
            Transform({-0.3f, 0.25f, 0.2f}, {1.9f, 1.2f, 1}, 0.27f), texture, {0.8f, 1, 0.6f, 0.7f}, 1.5f);
        Renderer2D::EndScene();
        Renderer->EndScene();
        EXPECT_EQ(Renderer2D::GetStats().DrawCalls, 1u);
        EXPECT_EQ(Renderer2D::GetStats().QuadCount, 1u);
        CompareScene("renderer2d-sprite", {1, 0.0});
    }
    TEST_F(RendererReferenceGL, FullRuntimeFrameIsByteIdenticalToPhaseOne)
    {
        // Baseline from the unmodified phase-one runtime path. All attachment bytes
        // detect moving sprites/grid/sky after materials, shell state, or outline order.
        Renderer2D::Init();
        SpriteInitialized                                  = true;
        auto camera                                        = Logic->CreateEntity("frame camera");
        camera.GetComponent<TransformComponent>().Position = {0, 1.4f, 4};
        camera.GetComponent<TransformComponent>().Rotation = {-.32f, 0, 0};
        camera.AddComponent<CameraComponent>().SceneCamera.SetPerspectve(glm::radians(45.0f), .1f, 20);
        auto light                                        = Logic->CreateEntity("frame light");
        light.GetComponent<TransformComponent>().Position = Info.pointLight.Position;
        auto& lighting                                    = light.AddComponent<PointLightComponent>();
        lighting.Ambient_Intensity                        = Info.pointLight.Ambient_Intensity;
        lighting.Diffuse_Intensity                        = Info.pointLight.Diffuse_Intensity;
        lighting.Specular_Intensity                       = Info.pointLight.Specular_Intensity;
        auto model                                        = Logic->CreateEntity("frame model");
        model.AddComponent<ProductModelComponent>(Sphere);
        model.GetComponent<TransformComponent>().Scale     = glm::vec3(.7f);
        auto sprite                                        = Logic->CreateEntity("frame sprite");
        sprite.GetComponent<TransformComponent>().Position = {-.3f, .2f, .5f};
        sprite.GetComponent<TransformComponent>().Scale    = {1.2f, .8f, 1};
        sprite.AddComponent<SpriteRendererComponent>(glm::vec4(.8f, .4f, .2f, .7f),
                                                     Renderer->GetAssetCache()->GetTexture(Albedo));
        auto runtime = CreateRef<SceneRenderer>(Render.get());
        runtime->SetAssetManager(Assets.get());
        runtime->PrepareEnvironment({32, 8, 32, 32});
        // Prewarm the same ready assets; this test isolates frame structure from async loading.
        for (auto id : Models)
            ASSERT_TRUE(runtime->GetAssetCache()->GetModel(id));
        for (auto id : Meshes)
            ASSERT_TRUE(runtime->GetAssetCache()->GetMesh(id));
        for (auto id : Materials)
            ASSERT_TRUE(runtime->GetAssetCache()->GetMaterialBindings(id));
        RenderResource settings {};
        settings.Piplinestate.EnableStencil     = true;
        settings.Piplinestate.EnableOutline     = true;
        settings.Piplinestate.EnableDebugNormal = true;
        Render->SetRenderResource(settings);
        runtime->SetHighlight({{Sphere, glm::scale(glm::mat4(1), glm::vec3(.7f))}});
        runtime->SetPreview(Quad, Transform({.6f, -.4f, .3f}, {.4f, .3f, 1}));
        Logic->OnViewportResize(Size, Size);
        Render->OnRenderRuntime(runtime, TimeStep(0));
        EXPECT_EQ(runtime->GetStats().DrawCalls, 5u);
        EXPECT_EQ(runtime->GetStats().Triangles, 4098u);
        EXPECT_EQ(Renderer2D::GetStats().QuadCount, 1u);
        EXPECT_EQ(Renderer2D::GetStats().DrawCalls, 1u);
        auto                                                         target = runtime->GetRenderTarget();
        std::vector<RenderHardware::Handle<RenderHardware::Texture>> attachments {
            target.Colors[0].Target, target.Colors[1].Target, target.Depth.Target};
        for (size_t i = 0; i < attachments.size(); ++i)
        {
            auto device = runtime->GetDevice();
            auto buffer = device->CreateBuffer(
                {Size * Size * 4, RenderHardware::BufferUsage::CopyDestination, RenderHardware::MemoryUsage::Readback});
            auto commands = device->CreateCommandList();
            commands->CopyTextureToBuffer(attachments[i], {}, buffer);
            device->Submit(commands);
            device->WaitIdle();
            auto                   bytes = buffer->Read(0, Size * Size * 4);
            std::vector<std::byte> digestBytes(bytes.size());
            std::memcpy(digestBytes.data(), bytes.data(), bytes.size());
            const char* expected[] = {"527d5095838ce92fa68ccd0d17a8f019c9e7c6140027255d8d72bd5f9eb04540",
                                      "d1b4803bb70e5b8e4a1b68ed184ddfd2a8fe31cba2a72561c6870b0da2feb6db",
                                      "62d92c7f002105fa75ac102c333e394e9b3706a8c5ad7a9da310d49680544988"};
            EXPECT_EQ(ComputeContentDigest(digestBytes), expected[i]) << "attachment " << i;
        }
        const auto&              graph = runtime->GetRenderGraph();
        std::vector<std::string> passes;
        for (auto pass : graph.ExecutionOrder())
            passes.push_back(graph.PassName(pass));
        EXPECT_EQ(passes,
                  (std::vector<std::string> {"Clear",
                                             "Renderer2D sprites",
                                             "Grid",
                                             "Skybox",
                                             "Materials (opaque/mask, blend)",
                                             "Legacy geometry",
                                             "Selection mask",
                                             "Selection composite",
                                             "Preview mask",
                                             "Preview composite"}));
        RenderHardware::Handle<RenderHardware::Texture> mask;
        size_t                                          maskCount = 0;
        for (size_t i = 0; i < graph.Resources().size(); ++i)
            if (!graph.Resources()[i].Imported)
            {
                ++maskCount;
                if (mask)
                    EXPECT_EQ(graph.Texture(uint32_t(i)), mask); // Selection and preview alias.
                mask = graph.Texture(uint32_t(i));
            }
        EXPECT_EQ(maskCount, 2u);
        ASSERT_TRUE(mask);
        // Catches recreating masks each frame even though viewport and lifetimes are stable.
        for (int frame = 0; frame < 5; ++frame)
        {
            if (frame == 2)
            {
                // Deselect for one frame; selecting again must reuse the stable-viewport mask.
                Render->OnRenderRuntime(runtime, TimeStep(0));
                for (const auto& resource : runtime->GetRenderGraph().Resources())
                    EXPECT_TRUE(resource.Imported);
                EXPECT_EQ(runtime->GetStats().DrawCalls, 4u);
            }
            runtime->SetHighlight({{Sphere, glm::scale(glm::mat4(1), glm::vec3(.7f))}});
            runtime->SetPreview(Quad, Transform({.6f, -.4f, .3f}, {.4f, .3f, 1}));
            Render->OnRenderRuntime(runtime, TimeStep(0));
            const auto& next = runtime->GetRenderGraph();
            for (size_t i = 0; i < next.Resources().size(); ++i)
                if (!next.Resources()[i].Imported)
                    EXPECT_EQ(next.Texture(uint32_t(i)), mask);
            EXPECT_EQ(runtime->GetStats().DrawCalls, 5u);
            EXPECT_EQ(runtime->GetStats().Triangles, 4098u);
        }
        for (auto entity : {sprite, model, light, camera})
            Logic->DestroyEntity(entity);
    }
} // namespace
